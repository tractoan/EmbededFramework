// Test nghiệp vụ thuần: không thread, không phần cứng – chỉ đưa event vào và kiểm tra kết quả.
#include "alarm_controller/alarm_controller.hpp"
#include "app_params.hpp"
#include "mocks.hpp"
#include "test_framework.hpp"

namespace {
using app::events::AlarmLevel;

struct Fixture {
    svc::EventBus bus;
    svc::ParamStore params;
    svc::CommandService cli;
    svc::LedService leds;
    app::Devices devices;
    app::SystemContext ctx{bus, params, cli, leds, devices};
    mock::Gpio led;
    mock::Recorder rec;

    Fixture() {
        params.init(nullptr, &bus);
        leds.bind(svc::LedId::Alarm, &led);
        bus.subscribe(app::events::kAlarmStateChanged, &rec);
    }
    static svc::Event temp(std::int32_t deci) {
        return svc::Event::make(app::events::kTemperature, app::events::TemperatureSample{deci});
    }
};
}  // namespace

TEST(alarm_thresholds_hysteresis_and_latch) {
    Fixture f;
    app::AlarmController::Config cfg;   // warn 60.0, high 75.0, hyst 2.0, latch
    app::AlarmController ac(cfg);
    CHECK(core::is_ok(ac.on_init(f.ctx)));
    CHECK(core::is_ok(ac.on_start()));
    CHECK(ac.level() == AlarmLevel::Normal);

    ac.on_event(Fixture::temp(500));
    CHECK(ac.level() == AlarmLevel::Normal);
    ac.on_event(Fixture::temp(650));
    CHECK(ac.level() == AlarmLevel::Warning);
    CHECK(f.leds.get(svc::LedId::Alarm) == svc::LedPattern::BlinkSlow);
    ac.on_event(Fixture::temp(590));   // trong vùng trễ -> giữ Warning
    CHECK(ac.level() == AlarmLevel::Warning);
    ac.on_event(Fixture::temp(800));
    CHECK(ac.level() == AlarmLevel::Alarm);
    ac.on_event(Fixture::temp(650));   // hết điều kiện nhưng đang latch
    CHECK(ac.level() == AlarmLevel::Alarm);
    ac.on_event(svc::Event::make(app::events::kAlarmAckRequest));
    CHECK(ac.level() == AlarmLevel::Warning);
    ac.on_event(Fixture::temp(570));
    CHECK(ac.level() == AlarmLevel::Normal);
    CHECK(f.leds.get(svc::LedId::Alarm) == svc::LedPattern::Off);
    CHECK_EQ(f.rec.events.size(), 4u);   // N->W, W->A, A->W, W->N
}

TEST(alarm_runtime_reconfiguration_and_fault) {
    Fixture f;
    app::AlarmController ac(app::AlarmController::Config{});
    ac.on_init(f.ctx);
    ac.on_start();

    // Tắt latch lúc chạy qua ParamStore (giống lệnh CLI "param set alarm.latch 0")
    CHECK(core::is_ok(f.params.set(app::params::kAlarmLatch, 0)));
    ac.on_event(svc::Event::make(svc::evt::kParamChanged, svc::ParamChanged{app::params::kAlarmLatch, 0}));
    ac.on_event(Fixture::temp(800));
    CHECK(ac.level() == AlarmLevel::Alarm);
    ac.on_event(Fixture::temp(500));   // không latch -> tự về Normal
    CHECK(ac.level() == AlarmLevel::Normal);

    ac.on_event(svc::Event::make(app::events::kSensorFault));
    CHECK(ac.level() == AlarmLevel::Fault);
    ac.on_event(Fixture::temp(900));   // bỏ qua dữ liệu khi đang Fault
    CHECK(ac.level() == AlarmLevel::Fault);
    ac.on_event(svc::Event::make(app::events::kSensorRecovered));
    CHECK(ac.level() == AlarmLevel::Normal);
}
