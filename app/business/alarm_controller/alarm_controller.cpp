#include "alarm_controller/alarm_controller.hpp"

#include <cstring>

#include "app_params.hpp"
#include "services/logger.hpp"
#include "utils/fixed_point.hpp"

namespace app {

namespace {
constexpr const char* kTag = "alarm";
}
using events::AlarmLevel;
using Signal = StateMachine<AlarmController>::Signal;

AlarmController::AlarmController(const Config& cfg)
    : cfg_(cfg),
      defs_{{
          {params::kAlarmWarnDeci, "alarm.warn", cfg.warn_deci, -400, 1500, "0.1C"},
          {params::kAlarmHighDeci, "alarm.high", cfg.high_deci, -400, 1500, "0.1C"},
          {params::kAlarmHysteresisDeci, "alarm.hyst", cfg.hysteresis_deci, 0, 200, "0.1C"},
          {params::kAlarmLatch, "alarm.latch", cfg.latch ? 1 : 0, 0, 1, "bool"},
      }},
      fsm_(*this, &AlarmController::st_normal) {}

core::Status AlarmController::on_init(SystemContext& ctx) {
    ctx_ = &ctx;
    core::Status st = ctx.params.register_params(defs_.data(), defs_.size());
    if (!core::is_ok(st)) return st;
    return ctx.cli.register_command({"alarm", "status | ack", this});
}

core::Status AlarmController::on_start() {
    apply_params();
    fsm_.start();
    return core::Status::Ok;
}

void AlarmController::apply_params() {
    warn_  = ctx_->params.get_or(params::kAlarmWarnDeci, cfg_.warn_deci);
    high_  = ctx_->params.get_or(params::kAlarmHighDeci, cfg_.high_deci);
    hyst_  = ctx_->params.get_or(params::kAlarmHysteresisDeci, cfg_.hysteresis_deci);
    latch_ = ctx_->params.get_or(params::kAlarmLatch, cfg_.latch ? 1 : 0) != 0;
    if (high_ <= warn_) LOGW(kTag, "alarm.high (%ld) <= alarm.warn (%ld)", static_cast<long>(high_), static_cast<long>(warn_));
}

void AlarmController::on_event(const msg::Event& e) {
    svc::ParamChanged pc{};
    if (e.id == svc::evt::kParamChanged && e.get(pc) && (pc.id & 0xFF00u) == 0x1100u) {
        apply_params();
        return;
    }
    fsm_.dispatch(e);
}

bool AlarmController::temperature_of(const msg::Event* e, std::int32_t& out) {
    events::TemperatureSample s{};
    if (e == nullptr || e->id != events::kTemperature || !e->get(s)) return false;
    last_temp_ = s.deci_celsius;
    out        = s.deci_celsius;
    return true;
}

void AlarmController::enter_level(AlarmLevel lvl) {
    const AlarmLevel prev = level_.exchange(lvl);
    svc::LedPattern pattern = svc::LedPattern::Off;
    switch (lvl) {
        case AlarmLevel::Normal:  pattern = svc::LedPattern::Off; break;
        case AlarmLevel::Warning: pattern = svc::LedPattern::BlinkSlow; break;
        case AlarmLevel::Alarm:   pattern = svc::LedPattern::BlinkFast; break;
        case AlarmLevel::Fault:   pattern = svc::LedPattern::On; break;
    }
    ctx_->leds.set(svc::LedId::Alarm, pattern);
    if (prev == lvl) return;

    const utils::Fixed t(last_temp_, 1);
    if (lvl == AlarmLevel::Normal) {
        LOGI(kTag, "%s -> %s (T=%s C)", events::to_string(prev), events::to_string(lvl), t.c_str());
    } else {
        LOGW(kTag, "%s -> %s (T=%s C)", events::to_string(prev), events::to_string(lvl), t.c_str());
    }
    ctx_->bus.publish(msg::Event::make(events::kAlarmStateChanged, events::AlarmStateChanged{prev, lvl, last_temp_}));
}

// ------------------------------------------------------------------ states
void AlarmController::st_normal(Signal sig, const msg::Event* e) {
    if (sig == Signal::Entry) {
        enter_level(AlarmLevel::Normal);
    } else if (sig == Signal::Event) {
        std::int32_t t = 0;
        if (temperature_of(e, t)) {
            if (t >= high_) {
                fsm_.transition(&AlarmController::st_alarm);
            } else if (t >= warn_) {
                fsm_.transition(&AlarmController::st_warning);
            }
        } else if (e->id == events::kSensorFault) {
            fsm_.transition(&AlarmController::st_fault);
        }
    }
}

void AlarmController::st_warning(Signal sig, const msg::Event* e) {
    if (sig == Signal::Entry) {
        enter_level(AlarmLevel::Warning);
    } else if (sig == Signal::Event) {
        std::int32_t t = 0;
        if (temperature_of(e, t)) {
            if (t >= high_) {
                fsm_.transition(&AlarmController::st_alarm);
            } else if (t < warn_ - hyst_) {
                fsm_.transition(&AlarmController::st_normal);
            }
        } else if (e->id == events::kSensorFault) {
            fsm_.transition(&AlarmController::st_fault);
        }
    }
}

void AlarmController::leave_alarm(std::int32_t t) {
    fsm_.transition(t >= warn_ ? &AlarmController::st_warning : &AlarmController::st_normal);
}

void AlarmController::st_alarm(Signal sig, const msg::Event* e) {
    if (sig == Signal::Entry) {
        acked_        = false;
        latch_notice_ = false;
        enter_level(AlarmLevel::Alarm);
        return;
    }
    if (sig != Signal::Event) return;

    std::int32_t t = 0;
    if (temperature_of(e, t)) {
        if (t < high_ - hyst_) {
            if (latch_ && !acked_) {
                if (!latch_notice_) LOGI(kTag, "condition cleared, waiting for 'alarm ack' (latched)");
                latch_notice_ = true;
            } else {
                leave_alarm(t);
            }
        }
    } else if (e->id == events::kAlarmAckRequest) {
        acked_ = true;
        LOGI(kTag, "alarm acknowledged");
        ctx_->leds.set(svc::LedId::Alarm, svc::LedPattern::On);
        if (last_temp_ < high_ - hyst_) leave_alarm(last_temp_);
    } else if (e->id == events::kSensorFault) {
        fsm_.transition(&AlarmController::st_fault);
    }
}

void AlarmController::st_fault(Signal sig, const msg::Event* e) {
    if (sig == Signal::Entry) {
        enter_level(AlarmLevel::Fault);
    } else if (sig == Signal::Event && e->id == events::kSensorRecovered) {
        fsm_.transition(&AlarmController::st_normal);
    }
}

// ---------------------------------------------------------------------- CLI
// Chạy trên thread CLI: KHÔNG chạm vào FSM trực tiếp -> gửi yêu cầu qua event.
int AlarmController::on_command(int argc, char** argv, svc::CommandOutput& out) {
    if (argc >= 2 && std::strcmp(argv[1], "ack") == 0) {
        ctx_->bus.publish(msg::Event::make(events::kAlarmAckRequest));
        out.print("ack requested\r\n");
        return 0;
    }
    out.print("level=%s warn=%ld high=%ld hyst=%ld latch=%ld (0.1C)\r\n", events::to_string(level()),
              static_cast<long>(ctx_->params.get_or(params::kAlarmWarnDeci, 0)),
              static_cast<long>(ctx_->params.get_or(params::kAlarmHighDeci, 0)),
              static_cast<long>(ctx_->params.get_or(params::kAlarmHysteresisDeci, 0)),
              static_cast<long>(ctx_->params.get_or(params::kAlarmLatch, 0)));
    return 0;
}

}  // namespace app
