#pragma once
#include <array>
#include <atomic>

#include "app/module.hpp"
#include "app/state_machine.hpp"
#include "app_events.hpp"

namespace app {

// Nghiệp vụ cảnh báo nhiệt – state machine:
//
//          T>=warn            T>=high
//  NORMAL ---------> WARNING ---------> ALARM
//    ^   <---------          <---------   |  (latch=1: phải "alarm ack" mới rời ALARM)
//    |   T<warn-hyst       T<high-hyst    |
//    |                                    |
//    +------- SensorRecovered --- FAULT <-+-- SensorFault (từ mọi trạng thái)
class AlarmController final : public IModule, public svc::ICommandHandler {
public:
    struct Config {
        std::int32_t warn_deci       = 600;   // 60.0 °C
        std::int32_t high_deci       = 750;   // 75.0 °C
        std::int32_t hysteresis_deci = 20;    //  2.0 °C
        bool latch                   = true;
    };

    explicit AlarmController(const Config& cfg);

    const char* name() const override { return "alarm_ctrl"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_event(const svc::Event& e) override;

    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "alarm"

    events::AlarmLevel level() const { return level_.load(); }

private:
    using Fsm = StateMachine<AlarmController>;

    void st_normal(Fsm::Signal sig, const svc::Event* e);
    void st_warning(Fsm::Signal sig, const svc::Event* e);
    void st_alarm(Fsm::Signal sig, const svc::Event* e);
    void st_fault(Fsm::Signal sig, const svc::Event* e);

    void enter_level(events::AlarmLevel lvl);
    void leave_alarm(std::int32_t t);
    bool temperature_of(const svc::Event* e, std::int32_t& out);
    void apply_params();

    Config cfg_;
    std::array<svc::ParamDef, 4> defs_;
    SystemContext* ctx_ = nullptr;
    Fsm fsm_;

    std::int32_t warn_ = 0, high_ = 0, hyst_ = 0;
    bool latch_        = true;
    bool acked_        = false;
    bool latch_notice_ = false;
    std::int32_t last_temp_ = 0;
    std::atomic<events::AlarmLevel> level_{events::AlarmLevel::Normal};
};

}  // namespace app
