#pragma once
// Nối tham số (ParamStore) -> cấu hình cảm biến, áp dụng NGAY:
//   - kParamsLoaded (sau khi nạp NV lúc khởi động): áp dụng giá trị đã lưu;
//   - kParamChanged trong dải [first, last] (CLI / Modbus / MAVLink / nghiệp vụ đổi tham số): áp dụng lại.
// Là listener TRỰC TIẾP của EventBus -> chạy trên thread vừa đổi tham số, không qua hàng đợi.
//   ApplyMode::Immediate (mặc định): ghi thiết bị ngay – configure() chờ khóa thiết bị tối đa lock_timeout_ms
//     (một lần đọc / một lát chờ của task chặn) + thời gian ghi; giới hạn, nhưng là ngoại lệ của quy tắc
//     "listener không chặn". last_status() = kết quả ghi.
//   ApplyMode::NextSample: chỉ gửi yêu cầu (không chặn); luồng lấy mẫu ghi ở lần lấy mẫu kế tiếp.
//     last_status() = yêu cầu đã được nhận chưa; kết quả ghi: last_status() của SensorConfigurator.
//
//   device::Tmp102Config tmp_from_params(const svc::ParamStore& p) {
//       device::Tmp102Config c;
//       c.rate     = static_cast<device::Tmp102Config::Rate>(p.get_or(kTmpRate, 2));
//       c.extended = p.get_or(kTmpExtended, 0) != 0;
//       return c;
//   }
//   static svc::SensorParamBinding<decltype(tmp_cfg), device::Tmp102Config> bind{tmp_cfg, params, kTmpRate,
//                                                                               kTmpExtended, &tmp_from_params,
//                                                                               svc::ApplyMode::NextSample};  // tùy chọn
//   bind.attach(bus);
#include <atomic>
#include <cstdint>

#include "messaging/event_bus.hpp"
#include "services/param_store.hpp"
#include "services/sensor_scheduler.hpp"

namespace svc {

template <typename Configurator, typename Config>
class SensorParamBinding final : public msg::IEventListener {
public:
    using Build = Config (*)(const ParamStore&);

    SensorParamBinding(Configurator& configurator, const ParamStore& params, ParamId first, ParamId last, Build build,
                       ApplyMode mode = ApplyMode::Immediate)
        : cfg_(configurator), params_(params), first_(first), last_id_(last), build_(build), mode_(mode) {}

    core::Status attach(msg::EventBus& bus) {
        core::Status st = bus.subscribe(evt::kParamsLoaded, this);
        if (core::is_ok(st)) st = bus.subscribe(evt::kParamChanged, this);
        return st;
    }

    void on_event(const msg::Event& e) override {
        ParamChanged pc{};
        const bool relevant = e.id == evt::kParamsLoaded ||
                              (e.id == evt::kParamChanged && e.get(pc) && pc.id >= first_ && pc.id <= last_id_);
        if (!relevant || build_ == nullptr) return;
        last_status_ = static_cast<std::int8_t>(cfg_.configure(build_(params_), mode_));
        applies_.fetch_add(1);
    }

    core::Status last_status() const { return static_cast<core::Status>(last_status_.load()); }
    std::uint32_t applies() const { return applies_.load(); }

private:
    Configurator& cfg_;
    const ParamStore& params_;
    ParamId first_;
    ParamId last_id_;
    Build build_;
    ApplyMode mode_;
    std::atomic<std::int8_t> last_status_{0};
    std::atomic<std::uint32_t> applies_{0};
};

// Chu kỳ lấy mẫu (task chặn: timeout) theo một tham số – áp dụng ngay (đánh thức scheduler).
class SensorPeriodBinding final : public msg::IEventListener {
public:
    SensorPeriodBinding(SensorTaskBase& task, const ParamStore& params, ParamId id) : task_(task), params_(params), id_(id) {}

    core::Status attach(msg::EventBus& bus) {
        core::Status st = bus.subscribe(evt::kParamsLoaded, this);
        if (core::is_ok(st)) st = bus.subscribe(evt::kParamChanged, this);
        return st;
    }
    void on_event(const msg::Event& e) override {
        ParamChanged pc{};
        if (e.id == evt::kParamsLoaded || (e.id == evt::kParamChanged && e.get(pc) && pc.id == id_)) {
            const std::int32_t ms = params_.get_or(id_, static_cast<std::int32_t>(task_.period_ms()));
            if (ms > 0) task_.set_period_ms(static_cast<std::uint32_t>(ms));
        }
    }

private:
    SensorTaskBase& task_;
    const ParamStore& params_;
    ParamId id_;
};

}  // namespace svc
