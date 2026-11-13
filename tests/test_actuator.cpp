// svc::Actuator<Cmd> / ActuatorHub: chế độ trực tiếp, thread riêng (servo PID nội bộ), arm, deadman,
// phát hiện thread kẹt, giám sát bằng thread thật.
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include "mocks.hpp"
#include "osal/memory.hpp"
#include "osal/time.hpp"
#include "services/actuator.hpp"
#include "test_framework.hpp"

namespace {
std::uint64_t g_clock = 0;
std::uint64_t fake_clock() { return g_clock; }

template <typename Pred>
bool wait_until(Pred p, int timeout_ms = 2000) {
    for (int i = 0; i < timeout_ms; ++i) {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return p();
}

// ---- Relay: chế độ TRỰC TIẾP
struct RelayCmd {
    bool on;
};
class Relay final : public svc::Actuator<RelayCmd> {
public:
    explicit Relay(svc::ActuatorClock clock = nullptr, std::uint32_t timeout = 0)
        : Actuator("relay", Config{0, timeout, 0, clock}) {}
    std::atomic<int> pin{-1};   // -1 = chưa ghi lần nào; 0 = mở (an toàn); 1 = đóng
    std::atomic<std::thread::id> writer{};
    std::atomic<int> on_writes{0};

private:
    void on_command(const RelayCmd& c, std::uint64_t) override {
        pin    = c.on ? 1 : 0;
        writer = std::this_thread::get_id();
        if (c.on) on_writes.fetch_add(1);
    }
    void write_safe() override { pin = 0; }
};

// ---- Servo tự làm: motor DC + cảm biến vị trí, PID NỘI BỘ trên thread riêng của servo
struct ServoCmd {
    float deg;
};
struct Plant {   // mô hình vật lý: vận tốc tỉ lệ điện áp, có quán tính bậc 1
    float pos = 0, vel = 0;
    void advance(float u, float dt) {
        vel += (u * 400.0f - vel) * dt / 0.02f;   // hằng số thời gian 20 ms, 400 °/s ở u = 1
        pos += vel * dt;
    }
};
class Servo final : public svc::Actuator<ServoCmd> {
public:
    Servo(Plant& plant, svc::ActuatorClock clock = nullptr, std::uint32_t timeout = 0, std::uint32_t period = 2)
        : Actuator("servo", Config{period, timeout, 0, clock}), plant_(plant) {}
    std::atomic<float> drive{0};          // "PWM" đang xuất, -1..1; 0 = an toàn
    std::atomic<int> non_safe_writes{0};
    std::atomic<bool> hang{false};        // mô phỏng thread bị kẹt trong on_update

private:
    void on_update(const ServoCmd& sp, std::uint64_t) override {
        while (hang.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const float dt  = static_cast<float>(config().update_period_ms) / 1000.0f;
        const float err = sp.deg - plant_.pos;
        integ_ += err * dt;
        float u = 0.05f * err + 0.02f * integ_ - 0.0015f * plant_.vel;   // PI + giảm chấn vận tốc
        u       = u > 1.0f ? 1.0f : (u < -1.0f ? -1.0f : u);
        drive   = u;
        if (u != 0.0f) non_safe_writes.fetch_add(1);
        plant_.advance(u, dt);
    }
    void write_safe() override {
        drive  = 0.0f;
        integ_ = 0.0f;   // không tích lũy sai số khi đang an toàn
    }
    Plant& plant_;
    float integ_ = 0;
};
}  // namespace

TEST(actuator_direct_mode_arm_and_command) {
    msg::EventBus bus;
    mock::Recorder rec;
    bus.subscribe(msg::evt::kAny, &rec);
    Relay relay;
    relay.set_event_bus(&bus);
    relay.write_safe_now();
    CHECK_EQ(relay.pin.load(), 0);
    CHECK(relay.reason() == svc::SafeReason::NotArmed);

    CHECK(relay.command({true}) == core::Status::Busy);   // chưa arm: từ chối, không chạm phần cứng
    CHECK_EQ(relay.pin.load(), 0);
    relay.arm();
    CHECK(relay.reason() == svc::SafeReason::NoCommand);
    CHECK(core::is_ok(relay.command({true})));
    CHECK_EQ(relay.pin.load(), 1);                          // ghi ngay ...
    CHECK(relay.writer.load() == std::this_thread::get_id());   // ... trên thread người gọi
    CHECK(relay.reason() == svc::SafeReason::None);
    CHECK_EQ(rec.count(svc::evt::kActuatorResumed), 1u);

    relay.disarm();
    CHECK_EQ(relay.pin.load(), 0);
    CHECK(relay.command({true}) == core::Status::Busy);
    CHECK_EQ(relay.pin.load(), 0);
    const svc::ActuatorStatus s = relay.status();
    CHECK(!s.threaded && !s.armed && s.commands == 1 && s.rejected == 2);
    svc::ActuatorEvent ev{};
    CHECK(rec.events.back().id == svc::evt::kActuatorSafe && rec.events.back().get(ev) &&
          ev.reason == svc::SafeReason::NotArmed);
}

// Chế độ trực tiếp: lệnh đã được nhận nhưng force_safe() chen vào trước khi kịp ghi phần cứng -> lần ghi đó
// phải bị bỏ. Sau khi force_safe() trả về, không được có thêm lần "đóng" nào (race – lặp nhiều lần).
TEST(actuator_direct_no_output_after_force_safe_returns) {
    Relay relay;
    int violations = 0;
    for (int round = 0; round < 300; ++round) {
        relay.arm();
        std::atomic<bool> go{true};
        std::thread commander([&] {
            while (go.load()) relay.command({true});
        });
        while (relay.on_writes.load() == 0) std::this_thread::yield();
        relay.force_safe(svc::SafeReason::Stalled);   // chốt: mọi lệnh sau đó bị từ chối
        const int after = relay.on_writes.load();
        for (int i = 0; i < 200; ++i) std::this_thread::yield();
        if (relay.on_writes.load() != after || relay.pin.load() != 0) ++violations;
        go = false;
        commander.join();
        relay.on_writes = 0;
    }
    CHECK_EQ(violations, 0);
}

TEST(actuator_deadman_forces_safe_and_new_command_resumes) {
    g_clock = 0;
    msg::EventBus bus;
    mock::Recorder rec;
    bus.subscribe(msg::evt::kAny, &rec);
    Relay relay(&fake_clock, 50);
    relay.set_event_bus(&bus);
    relay.arm();
    g_clock = 10;
    relay.command({true});
    relay.supervise(40);
    CHECK_EQ(relay.pin.load(), 1);                          // 30 ms < 50 ms: còn hiệu lực
    relay.supervise(70);
    CHECK_EQ(relay.pin.load(), 0);                          // 60 ms > 50 ms: deadman
    CHECK(relay.reason() == svc::SafeReason::CommandTimeout);
    CHECK_EQ(rec.count(svc::evt::kActuatorSafe), 1u);
    relay.supervise(90);
    CHECK_EQ(rec.count(svc::evt::kActuatorSafe), 1u);       // không phát lặp
    g_clock = 95;
    CHECK(core::is_ok(relay.command({true})));               // lệnh mới: tự xuất lại (không cần arm lại)
    CHECK_EQ(relay.pin.load(), 1);
    CHECK(relay.reason() == svc::SafeReason::None);
    CHECK_EQ(rec.count(svc::evt::kActuatorResumed), 2u);
}

TEST(actuator_servo_runs_its_own_pid_and_converges) {
    g_clock = 0;
    Plant plant;
    Servo servo(plant, &fake_clock);
    CHECK(servo.threaded());
    servo.arm();
    for (int i = 0; i < 50; ++i, g_clock += 2) servo.step(g_clock);   // chưa có lệnh: giữ an toàn
    CHECK(servo.drive.load() == 0.0f && plant.pos == 0.0f);
    CHECK(servo.reason() == svc::SafeReason::NoCommand);

    CHECK(core::is_ok(servo.command({30.0f})));
    float peak = 0;
    for (int i = 0; i < 1500; ++i, g_clock += 2) {   // 3 s mô phỏng, vòng 500 Hz
        servo.step(g_clock);
        peak = plant.pos > peak ? plant.pos : peak;
    }
    std::printf("    servo PID: pos=%.2f deg (setpoint 30), overshoot=%.1f%%, drive=%.3f\n", plant.pos,
                (peak - 30.0f) / 30.0f * 100.0f, servo.drive.load());
    CHECK(std::fabs(plant.pos - 30.0f) < 0.5f);
    CHECK(peak < 36.0f);   // vọt lố < 20 %
    CHECK_EQ(servo.status().updates, 1500u);
    servo.command({-10.0f});   // đổi setpoint lúc đang chạy
    for (int i = 0; i < 1500; ++i, g_clock += 2) servo.step(g_clock);
    CHECK(std::fabs(plant.pos + 10.0f) < 0.5f);
}

TEST(actuator_threaded_deadman_runs_on_own_thread) {
    g_clock = 0;
    Plant plant;
    Servo servo(plant, &fake_clock, 30, 10);   // thread riêng 10 ms, deadman 30 ms
    servo.arm();
    servo.command({20.0f});
    for (int t = 0; t <= 30; t += 10) {
        g_clock = static_cast<std::uint64_t>(t);
        servo.step(g_clock);
    }
    CHECK(servo.reason() == svc::SafeReason::None && servo.drive.load() != 0.0f);
    g_clock = 40;
    servo.step(g_clock);   // 40 ms không có lệnh mới > 30 ms: chính thread của servo tự về an toàn
    CHECK(servo.reason() == svc::SafeReason::CommandTimeout);
    CHECK(servo.drive.load() == 0.0f);
    g_clock = 45;
    CHECK(core::is_ok(servo.command({20.0f})));
    g_clock = 50;
    servo.step(g_clock);
    CHECK(servo.reason() == svc::SafeReason::None && servo.drive.load() != 0.0f);

    // Chưa start(): supervisor không coi là "thread kẹt" (chỉ áp deadman)
    servo.supervise(500);
    CHECK(servo.reason() == svc::SafeReason::CommandTimeout);
}

TEST(actuator_hub_supervisor_detects_stall_and_deadman_with_real_threads) {
    msg::EventBus bus;
    Plant plant;
    Servo servo(plant, nullptr, 40, 2);   // thread riêng 2 ms, deadman 40 ms, kẹt sau 20 ms
    Relay relay(nullptr, 40);
    svc::ActuatorHub hub;
    CHECK(core::is_ok(hub.add(&servo, {"servo", osal::Priority::High, 2048, nullptr})));
    CHECK(core::is_ok(hub.add(&relay)));
    hub.write_safe_all();
    CHECK(core::is_ok(hub.start(&bus, {"act_sup", osal::Priority::Realtime, 1024, nullptr}, 2)));
    servo.arm();
    relay.arm();

    // 1) có lệnh đều -> chạy; ngừng gửi lệnh -> deadman (cả thread riêng lẫn trực tiếp)
    for (int i = 0; i < 50; ++i) {
        servo.command({15.0f});
        relay.command({true});
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(servo.reason() == svc::SafeReason::None && relay.pin.load() == 1);
    CHECK(servo.status().updates > 20);
    CHECK(wait_until([&] { return servo.reason() == svc::SafeReason::CommandTimeout; }, 500));
    CHECK(wait_until([&] { return relay.reason() == svc::SafeReason::CommandTimeout; }, 500));
    CHECK(servo.drive.load() == 0.0f && relay.pin.load() == 0);

    // 2) thread của servo kẹt bên trong on_update (đang giữ khóa phần cứng) -> supervisor phát hiện, chốt an toàn
    servo.command({15.0f});
    CHECK(wait_until([&] { return servo.drive.load() != 0.0f; }));
    servo.hang = true;
    CHECK(wait_until([&] { return servo.reason() == svc::SafeReason::Stalled; }, 1000));
    CHECK(servo.command({15.0f}) == core::Status::Busy);   // bị chốt: từ chối lệnh
    servo.hang = false;                                     // thread "tỉnh lại"
    // Lần ghi đang dở lúc kẹt có thể còn hoàn tất, nhưng chu kỳ kế tiếp phải về an toàn và GIỮ an toàn:
    CHECK(wait_until([&] { return servo.drive.load() == 0.0f; }, 500));
    const int writes = servo.non_safe_writes.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK_EQ(servo.non_safe_writes.load(), writes);
    CHECK(servo.drive.load() == 0.0f);

    servo.arm();   // người vận hành arm lại -> hoạt động lại
    CHECK(core::is_ok(servo.command({15.0f})));
    CHECK(wait_until([&] { return servo.drive.load() != 0.0f; }));

    hub.stop();   // tắt máy: mọi actuator về an toàn, thread đã dừng
    CHECK(servo.drive.load() == 0.0f && relay.pin.load() == 0);
    CHECK(servo.reason() == svc::SafeReason::Shutdown && relay.reason() == svc::SafeReason::Shutdown);
    CHECK(servo.command({15.0f}) == core::Status::Busy);
}

TEST(actuator_hub_start_failure_leaves_everything_safe) {
    Plant p1, p2;
    Servo ok(p1), bad(p2);
    svc::ActuatorHub hub;
    hub.add(&ok, {"ok", osal::Priority::High, 2048, nullptr});
    hub.add(&bad, {"bad", osal::Priority::High, osal::stack_pool_stats().capacity * 2, nullptr});   // vượt pool
    const std::size_t threads_before = osal::thread_count();
    CHECK(!core::is_ok(hub.start(nullptr, {"act_sup", osal::Priority::Realtime, 1024, nullptr})));
    CHECK_EQ(osal::thread_count(), threads_before);
    CHECK(ok.reason() == svc::SafeReason::Shutdown && ok.drive.load() == 0.0f);
    CHECK(ok.command({1.0f}) == core::Status::Busy);
    CHECK(hub.add(nullptr) == core::Status::InvalidArg);
}
