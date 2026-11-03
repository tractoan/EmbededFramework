#include "temperature_monitor/temperature_monitor.hpp"

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
    LOGD(kTag, "period=%lu ms offset=%ld filter=%u", static_cast<unsigned long>(period_ms_),
         static_cast<long>(offset_), static_cast<unsigned>(window_));
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

void TemperatureMonitor::on_tick(std::uint64_t /*now_ms*/) {
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
}

int TemperatureMonitor::on_command(int /*argc*/, char** /*argv*/, svc::CommandOutput& out) {
    if (has_value_) {
        out.print("temperature: %s C\r\n", utils::Fixed(last_value_.load(), 1).c_str());
    } else {
        out.print("temperature: n/a\r\n");
    }
    out.print("samples=%lu errors=%lu\r\n", static_cast<unsigned long>(samples_.load()),
              static_cast<unsigned long>(errors_.load()));
    return 0;
}

}  // namespace app
