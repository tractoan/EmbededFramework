#include "mocks.hpp"
#include "messaging/event_bus.hpp"
#include "test_framework.hpp"

TEST(event_bus_routes_by_id_and_wildcard) {
    msg::EventBus bus;
    mock::Recorder only_a, all;
    constexpr msg::EventId kA = 0x2001, kB = 0x2002;
    CHECK(core::is_ok(bus.subscribe(kA, &only_a)));
    CHECK(core::is_ok(bus.subscribe(msg::evt::kAny, &all)));
    CHECK(core::is_ok(bus.subscribe(kA, &only_a)));   // trùng -> bỏ qua
    CHECK_EQ(bus.subscriber_count(), 2u);

    struct P { std::int32_t x; std::int16_t y; };
    bus.publish(msg::Event::make(kA, P{42, -7}));
    bus.publish(msg::Event::make(kB));

    CHECK_EQ(only_a.events.size(), 1u);
    CHECK_EQ(all.events.size(), 2u);
    P p{};
    CHECK(only_a.events[0].get(p) && p.x == 42 && p.y == -7);
    std::int16_t wrong = 0;
    CHECK(!only_a.events[0].get(wrong));   // sai kiểu payload -> từ chối

    CHECK(core::is_ok(bus.unsubscribe(kA, &only_a)));
    bus.publish(msg::Event::make(kA));
    CHECK_EQ(only_a.events.size(), 1u);
}
