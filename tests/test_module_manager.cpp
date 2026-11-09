#include "app/module_manager.hpp"
#include "mocks.hpp"
#include "test_framework.hpp"

namespace {
class CountingModule final : public app::IModule {
public:
    const char* name() const override { return "counting"; }
    core::Status on_init(app::SystemContext&) override {
        ++inits;
        return core::Status::Ok;
    }
    void on_event(const msg::Event& e) override {
        if (e.id == 0x2100) ++events;
    }
    std::uint32_t tick_period_ms() const override { return 10; }
    void on_tick(std::uint64_t) override { ++ticks; }
    int inits = 0, events = 0, ticks = 0;
};
}  // namespace

TEST(module_manager_dispatches_events_and_ticks) {
    msg::EventBus bus;
    svc::ParamStore params;
    svc::CommandService cli;
    svc::LedService leds;
    app::Devices devices;
    app::SystemContext ctx{bus, params, cli, leds, devices};

    app::ModuleManager mm;
    CountingModule m;
    CHECK(core::is_ok(mm.add(&m)));
    CHECK(core::is_ok(mm.init_all(ctx)));
    CHECK_EQ(m.inits, 1);

    bus.publish(msg::Event::make(0x2100));   // từ "thread khác" -> vào queue
    bus.publish(msg::Event::make(0x2200));
    CHECK_EQ(m.events, 0);                    // chưa xử lý cho tới khi app thread chạy
    mm.process(0);
    mm.process(0);
    CHECK_EQ(m.events, 1);
    CHECK(m.ticks >= 1);
    const int t = m.ticks;
    for (int i = 0; i < 5; ++i) mm.process(20);
    CHECK(m.ticks > t);
}
