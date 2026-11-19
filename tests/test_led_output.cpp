// LED là một output loại Indicator (LedOutput) + mặt tiền LedService cho nghiệp vụ.
#include <atomic>
#include <chrono>
#include <thread>

#include "mocks.hpp"
#include "osal/memory.hpp"
#include "services/led_service.hpp"
#include "test_framework.hpp"

namespace {
// GPIO an toàn đa luồng (thread của LedOutput ghi, test đọc).
class AtomicGpio final : public hal::IGpio {
public:
    core::Status configure(hal::PinMode) override { return core::Status::Ok; }
    core::Status write(bool level) override {
        level_ = level;
        writes.fetch_add(1);
        return core::Status::Ok;
    }
    bool read() const override { return level_.load(); }
    std::atomic<int> writes{0};

private:
    std::atomic<bool> level_{false};
};

template <typename Pred>
bool wait_until(Pred p, int timeout_ms = 2000) {
    for (int i = 0; i < timeout_ms; ++i) {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return p();
}
}  // namespace

TEST(led_patterns_levels) {
    using svc::LedPattern;
    CHECK(!svc::led_level(LedPattern::Off, 123));
    CHECK(svc::led_level(LedPattern::On, 123));
    CHECK(!svc::led_level(LedPattern::BlinkSlow, 499) && svc::led_level(LedPattern::BlinkSlow, 500));
    CHECK(!svc::led_level(LedPattern::BlinkFast, 124) && svc::led_level(LedPattern::BlinkFast, 125) &&
          !svc::led_level(LedPattern::BlinkFast, 250));
    CHECK(svc::led_level(LedPattern::Heartbeat, 1079) && !svc::led_level(LedPattern::Heartbeat, 1080));
}

TEST(led_output_is_an_indicator_driving_all_leds_in_one_frame) {
    mock::Gpio status, alarm;
    svc::LedService leds;
    leds.bind(svc::LedId::Status, &status);
    leds.bind(svc::LedId::Alarm, &alarm);
    svc::LedOutput& out = leds.output();
    CHECK(out.output_class() == svc::OutputClass::Indicator);
    CHECK(out.threaded() && out.armed());               // chỉ báo: không cần arm
    CHECK(!out.needs_supervision());

    out.write_safe_now();
    CHECK(!status.read() && !alarm.read());
    leds.set(svc::LedId::Status, svc::LedPattern::On);
    leds.set(svc::LedId::Alarm, svc::LedPattern::BlinkFast);   // LED khác không ghi đè LED đã đặt
    CHECK(leds.get(svc::LedId::Status) == svc::LedPattern::On);
    CHECK(leds.get(svc::LedId::Alarm) == svc::LedPattern::BlinkFast);
    out.step(130);                                     // thread của LedOutput (đồng hồ do test cấp)
    CHECK(status.read() && alarm.read());
    const int writes = status.writes;
    out.step(180);                                     // mức không đổi: không ghi lại
    CHECK_EQ(status.writes, writes);
    out.step(260);
    CHECK(status.read() && !alarm.read());             // BlinkFast: tắt ở 250..374
    CHECK(out.reason() == svc::SafeReason::None);
}

TEST(led_indicator_survives_failsafe_and_turns_off_at_shutdown) {
    AtomicGpio status, alarm;
    svc::LedService leds;
    leds.bind(svc::LedId::Status, &status);
    leds.bind(svc::LedId::Alarm, &alarm);
    svc::OutputHub hub;
    CHECK(core::is_ok(hub.add(&leds.output(), {"led", osal::Priority::Low, 1024, nullptr})));
    hub.write_safe_all();
    const std::size_t threads_before = osal::thread_count();
    CHECK(core::is_ok(hub.start(nullptr, {"out_sup", osal::Priority::Realtime, 1024, nullptr})));
    CHECK(!hub.supervising());                                    // chỉ indicator: không tạo thread giám sát
    CHECK_EQ(osal::thread_count(), threads_before + 1);           // chỉ thread của LED

    leds.set(svc::LedId::Alarm, svc::LedPattern::On);
    CHECK(wait_until([&] { return alarm.read(); }));
    hub.disarm_all();                                             // failsafe: KHÔNG được tắt đèn cảnh báo
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(alarm.read());
    CHECK(leds.output().reason() == svc::SafeReason::None);

    hub.stop();                                                   // tắt máy: mọi LED tắt
    CHECK(!alarm.read() && !status.read());
    CHECK(leds.output().reason() == svc::SafeReason::Shutdown);
    leds.set(svc::LedId::Alarm, svc::LedPattern::On);             // sau khi tắt: bỏ qua, không bật lại
    CHECK(!alarm.read());
}
