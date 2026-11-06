#include "temperature_monitor/temperature_monitor.hpp"

#include <cstring>

#include "app_params.hpp"
#include "services/logger.hpp"
#include "utils/fixed_point.hpp"

namespace app {

namespace {
constexpr const char* kTag = "temp";
}

TemperatureMonitor::TemperatureMonitor(const Config& cfg)
    : cfg_(cfg),
      defs_{{
          {params::kTempSamplePeriodMs, "temp.period_ms", cfg.sample_period_ms, 100, 60000, "ms"},
          {params::kTempOffsetDeci, "temp.offset", 0, -100, 100, "0.1C"},
          {params::kTempFilterWindow, "temp.filter_n", cfg.filter_window, 1, static_cast<std::int32_t>(kMaxWindow), ""},
          {params::kTempBatchSize, "temp.batch_n", cfg.batch_size, 0, kMaxBatch, "samples"},
      }} {}

core::Status TemperatureMonitor::on_init(SystemContext& ctx) {
    ctx_ = &ctx;
    if (ctx.devices.temperature == nullptr) {
        LOGE(kTag, "no temperature sensor on this board");
        return core::Status::NotFound;
    }
    core::Status st = ctx.params.register_params(defs_.data(), defs_.size());
    if (!core::is_ok(st)) return st;
    return ctx.cli.register_command({"temp", "show temperature statistics", this});
}

core::Status TemperatureMonitor::on_start() {
    apply_params();
    return core::Status::Ok;
}

void TemperatureMonitor::apply_params() {
    period_ms_ = static_cast<std::uint32_t>(ctx_->params.get_or(params::kTempSamplePeriodMs, cfg_.sample_period_ms));
    offset_    = ctx_->params.get_or(params::kTempOffsetDeci, 0);
    const auto w = static_cast<std::size_t>(ctx_->params.get_or(params::kTempFilterWindow, cfg_.filter_window));
    if (w != window_) {
        window_     = w;
        hist_count_ = 0;   // đổi cửa sổ lọc -> reset lịch sử
        hist_pos_   = 0;
    }
    const auto n = static_cast<std::size_t>(ctx_->params.get_or(params::kTempBatchSize, cfg_.batch_size));
    if (n != batch_n_) {
        batch_n_ = n;
        batch_.reset();   // bỏ lô dở dang có kích thước cũ (trả bộ nhớ về allocator)
        alloc_warned_ = false;
    }
    LOGD(kTag, "period=%lu ms offset=%ld filter=%u batch=%u", static_cast<unsigned long>(period_ms_),
         static_cast<long>(offset_), static_cast<unsigned>(window_), static_cast<unsigned>(batch_n_));
}

void TemperatureMonitor::on_event(const svc::Event& e) {
    svc::ParamChanged pc{};
    if (e.id == svc::evt::kParamChanged && e.get(pc) && (pc.id & 0xFF00u) == 0x1000u) apply_params();
}

std::int32_t TemperatureMonitor::filter(std::int32_t sample) {
    history_[hist_pos_] = sample;
    hist_pos_ = (hist_pos_ + 1) % window_;
    if (hist_count_ < window_) ++hist_count_;
    std::int64_t sum = 0;
    for (std::size_t i = 0; i < hist_count_; ++i) sum += history_[i];
    return static_cast<std::int32_t>(sum / static_cast<std::int64_t>(hist_count_));
}

void TemperatureMonitor::add_to_batch(std::int32_t value, std::uint64_t now_ms) {
    if (!batch_) {
        batch_ = svc::Buffer::allocate(batch_n_ * sizeof(std::int16_t));
        if (!batch_) {
            batch_failures_.fetch_add(1);
            if (!alloc_warned_) LOGW(kTag, "cannot allocate batch of %u samples", static_cast<unsigned>(batch_n_));
            alloc_warned_ = true;
            return;
        }
        batch_.resize(0);
        batch_first_ms_ = static_cast<std::uint32_t>(now_ms);
    }
    const auto v = static_cast<std::int16_t>(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
    const std::size_t used = batch_.size();
    std::memcpy(batch_.data() + used, &v, sizeof(v));
    batch_.resize(used + sizeof(v));

    const std::size_t count = batch_.size() / sizeof(std::int16_t);
    if (count >= batch_n_) {
        const events::TemperatureBatch hdr{static_cast<std::uint16_t>(count),
                                           static_cast<std::uint16_t>(period_ms_ > 0xFFFF ? 0xFFFF : period_ms_),
                                           batch_first_ms_};
        // Chuyển quyền sở hữu buffer vào Event: từ đây chỉ đọc, mọi listener dùng chung.
        ctx_->bus.publish(svc::Event::make(events::kTemperatureBatch, hdr, std::move(batch_)));
        batch_.reset();
        batches_.fetch_add(1);
    }
}

void TemperatureMonitor::on_tick(std::uint64_t now_ms) {
    std::int32_t raw = 0;
    const core::Status st = ctx_->devices.temperature->read_deci_celsius(raw);
    if (!core::is_ok(st)) {
        errors_.fetch_add(1);
        if (++consecutive_errors_ >= cfg_.fault_threshold && !faulted_) {
            faulted_ = true;
            LOGE(kTag, "sensor fault (%s)", core::to_string(st));
            ctx_->bus.publish(svc::Event::make(events::kSensorFault));
        }
        return;
    }
    consecutive_errors_ = 0;
    if (faulted_) {
        faulted_ = false;
        LOGI(kTag, "sensor recovered");
        ctx_->bus.publish(svc::Event::make(events::kSensorRecovered));
    }

    const std::int32_t value = filter(raw + offset_);
    last_value_ = value;
    has_value_  = true;
    samples_.fetch_add(1);
    LOGD(kTag, "T=%s C", utils::Fixed(value, 1).c_str());
    ctx_->bus.publish(svc::Event::make(events::kTemperature, events::TemperatureSample{value}));
    if (batch_n_ > 0) add_to_batch(value, now_ms);
}

int TemperatureMonitor::on_command(int /*argc*/, char** /*argv*/, svc::CommandOutput& out) {
    if (has_value_) {
        out.print("temperature: %s C\r\n", utils::Fixed(last_value_.load(), 1).c_str());
    } else {
        out.print("temperature: n/a\r\n");
    }
    out.print("samples=%lu errors=%lu batches=%lu batch_alloc_failures=%lu\r\n",
              static_cast<unsigned long>(samples_.load()), static_cast<unsigned long>(errors_.load()),
              static_cast<unsigned long>(batches_.load()), static_cast<unsigned long>(batch_failures_.load()));
    return 0;
}

}  // namespace app
