#pragma once
// Module nghiệp vụ RIÊNG của sản phẩm thermo_pro (không nằm trong thư viện dùng chung):
// thống kê min/avg/max nhiệt độ và số lần cảnh báo theo chu kỳ cấu hình được.
#include <climits>

#include "app/module.hpp"
#include "app_events.hpp"
#include "app_params.hpp"
#include "services/logger.hpp"
#include "utils/fixed_point.hpp"

namespace product {

class StatsReporter final : public app::IModule {
public:
    static constexpr svc::ParamId kPeriodParam = app::params::kProductBase + 1;

    const char* name() const override { return "stats_reporter"; }

    core::Status on_init(app::SystemContext& ctx) override {
        ctx_ = &ctx;
        return ctx.params.register_params(&def_, 1);
    }
    core::Status on_start() override {
        period_ = static_cast<std::uint32_t>(ctx_->params.get_or(kPeriodParam, def_.default_value));
        return core::Status::Ok;
    }

    void on_event(const svc::Event& e) override {
        app::events::TemperatureSample s{};
        svc::ParamChanged pc{};
        if (e.id == app::events::kTemperature && e.get(s)) {
            min_ = s.deci_celsius < min_ ? s.deci_celsius : min_;
            max_ = s.deci_celsius > max_ ? s.deci_celsius : max_;
            sum_ += s.deci_celsius;
            ++n_;
        } else if (e.id == app::events::kAlarmStateChanged) {
            ++transitions_;
        } else if (e.id == svc::evt::kParamChanged && e.get(pc) && pc.id == kPeriodParam) {
            period_ = static_cast<std::uint32_t>(pc.value);
        }
    }

    std::uint32_t tick_period_ms() const override { return period_; }

    void on_tick(std::uint64_t /*now_ms*/) override {
        if (n_ == 0) return;
        LOGI("stats", "T min/avg/max = %s / %s / %s C over %lu samples, %lu alarm transitions", utils::Fixed(min_, 1).c_str(),
             utils::Fixed(static_cast<std::int32_t>(sum_ / n_), 1).c_str(), utils::Fixed(max_, 1).c_str(), static_cast<unsigned long>(n_),
             static_cast<unsigned long>(transitions_));
        min_ = INT32_MAX;
        max_ = INT32_MIN;
        sum_ = 0;
        n_ = transitions_ = 0;
    }

private:
    const svc::ParamDef def_{kPeriodParam, "stats.period_ms", 10000, 1000, 3600000, "ms"};
    app::SystemContext* ctx_ = nullptr;
    std::uint32_t period_ = 10000;
    std::int32_t min_ = INT32_MAX, max_ = INT32_MIN;
    std::int64_t sum_ = 0;
    std::uint32_t n_ = 0, transitions_ = 0;
};

}  // namespace product
