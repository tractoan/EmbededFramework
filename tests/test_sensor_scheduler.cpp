// msg::Topic<T> và svc::SensorScheduler / SensorTask<> / SensorHub.
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "mocks.hpp"
#include "messaging/topic.hpp"
#include "osal/memory.hpp"
#include "services/sensor_scheduler.hpp"
#include "test_framework.hpp"

namespace {
// Đồng hồ giả: test điều khiển thời gian -> kết quả lập lịch tất định.
std::uint64_t g_clock = 0;
std::uint64_t fake_clock() { return g_clock; }

// Cảm biến giả: mỗi lần đọc "tốn" exec_ms trên đồng hồ giả, có thể lập trình lỗi.
class FakeSensor {
public:
    core::Status read(std::int32_t& out) {
        g_clock += exec_ms;
        stamps.push_back(g_clock - exec_ms);
        if (fail) return core::Status::Timeout;
        out = ++value;
        return core::Status::Ok;
    }
    std::uint32_t exec_ms = 0;
    bool fail             = false;
    std::int32_t value    = 0;
    std::vector<std::uint64_t> stamps;   // thời điểm bắt đầu mỗi lần đọc
};
using FakeTask = svc::SensorTask<FakeSensor, std::int32_t, &FakeSensor::read>;

svc::SensorScheduler::Config fake_cfg(std::uint32_t threshold = 3) {
    svc::SensorScheduler::Config c;
    c.fault_threshold = threshold;
    c.clock           = &fake_clock;
    return c;
}

// Chạy scheduler tới thời điểm `until` trên đồng hồ giả, nhảy thẳng tới hạn kế tiếp.
void run_until(svc::SensorScheduler& s, std::uint64_t until) {
    while (g_clock <= until) {
        const std::uint32_t wait = s.run_due();
        g_clock += wait > 0 ? wait : 0;
        if (wait == 0 && g_clock > until) break;
    }
}

struct Pair {   // dữ liệu tự kiểm tra: một bản chụp rách sẽ có b != ~a hoặc c != 3a
    std::uint32_t a, b, c, pad;
};
}  // namespace

TEST(topic_latest_value_and_stamp) {
    msg::Topic<Pair> t;
    Pair p{};
    msg::TopicStamp st;
    CHECK(t.read(p) == core::Status::NotFound);
    CHECK_EQ(t.sequence(), 0u);
    t.publish(Pair{7, ~7u, 21, 0}, 1234);
    CHECK(core::is_ok(t.read(p, &st)));
    CHECK(p.a == 7 && p.b == ~7u && p.c == 21);
    CHECK(st.sequence == 1 && st.timestamp_ms == 1234);
    t.publish(Pair{8, ~8u, 24, 0}, 0x1'0000'0005ull);   // timestamp > 32 bit
    CHECK(core::is_ok(t.read(p, &st)));
    CHECK(p.a == 8 && st.sequence == 2 && st.timestamp_ms == 0x1'0000'0005ull);
    CHECK_EQ(t.sequence(), 2u);
}

TEST(topic_concurrent_readers_never_see_torn_values) {
    msg::Topic<Pair> t;
    constexpr std::uint32_t kWrites = 200000;
    std::atomic<bool> done{false};
    std::atomic<std::uint32_t> torn{0}, backwards{0}, reads{0}, busy{0};
    auto reader = [&] {
        std::uint32_t last_seq = 0;
        while (!done.load()) {
            Pair p{};
            msg::TopicStamp st;
            const core::Status r = t.read(p, &st);
            if (r == core::Status::Busy) busy.fetch_add(1);
            if (!core::is_ok(r)) continue;
            reads.fetch_add(1);
            if (p.b != ~p.a || p.c != p.a * 3u || st.timestamp_ms != p.a) torn.fetch_add(1);
            if (st.sequence < last_seq) backwards.fetch_add(1);
            last_seq = st.sequence;
        }
    };
    std::thread r1(reader), r2(reader), r3(reader);
    for (std::uint32_t i = 1; i <= kWrites; ++i) t.publish(Pair{i, ~i, i * 3u, 0}, i);
    done = true;
    r1.join();
    r2.join();
    r3.join();
    std::printf("    topic: %u writes, %u consistent reads, %u busy\n", kWrites, reads.load(), busy.load());
    CHECK_EQ(torn.load(), 0u);
    CHECK_EQ(backwards.load(), 0u);
    CHECK(reads.load() > 0);
    CHECK_EQ(t.sequence(), kWrites);
}

TEST(sensor_scheduler_runs_each_task_at_its_period) {
    g_clock = 0;
    FakeSensor fast_dev, slow_dev;
    msg::Topic<std::int32_t> fast_t, slow_t;
    FakeTask fast("fast", fast_dev, fast_t, 10);
    FakeTask slow("slow", slow_dev, slow_t, 25);
    svc::SensorScheduler s(fake_cfg());
    CHECK(core::is_ok(s.add(&fast)));
    CHECK(core::is_ok(s.add(&slow)));
    run_until(s, 100);
    CHECK_EQ(fast_dev.stamps.size(), 11u);   // 0,10,...,100
    CHECK_EQ(slow_dev.stamps.size(), 5u);    // 0,25,50,75,100
    for (std::size_t i = 0; i < fast_dev.stamps.size(); ++i) CHECK_EQ(fast_dev.stamps[i], i * 10u);
    std::int32_t v = 0;
    msg::TopicStamp st;
    CHECK(core::is_ok(fast_t.read(v, &st)) && v == 11 && st.sequence == 11 && st.timestamp_ms == 100);
    svc::SensorHealth h;
    CHECK(s.health(1, h) && std::string(h.name) == "slow" && h.samples_ok == 5 && h.errors == 0 && h.period_ms == 25);
    CHECK(!s.health(2, h));
}

TEST(sensor_scheduler_does_not_drift_and_resets_after_overrun) {
    g_clock = 0;
    FakeSensor dev;
    dev.exec_ms = 3;   // mỗi lần đọc tốn 3 ms: lịch "now + period" sẽ trôi thành 13 ms
    msg::Topic<std::int32_t> t;
    FakeTask task("imu", dev, t, 10);
    svc::SensorScheduler s(fake_cfg());
    s.add(&task);
    run_until(s, 95);
    CHECK_EQ(dev.stamps.size(), 10u);
    for (std::size_t i = 0; i < dev.stamps.size(); ++i) CHECK_EQ(dev.stamps[i], i * 10u);   // đúng mốc, không trôi

    // Thread bị chặn 35 ms (lỡ 3 chu kỳ): chỉ đọc MỘT lần (không chạy dồn), đếm overrun, lịch đặt lại.
    const std::size_t before = dev.stamps.size();
    g_clock                  = 100 + 35;
    s.run_due();
    CHECK_EQ(dev.stamps.size(), before + 1);
    s.run_due();
    CHECK_EQ(dev.stamps.size(), before + 1);
    svc::SensorHealth h;
    CHECK(s.health(0, h) && h.overruns == 1 && h.max_exec_ms == 3);
    g_clock = 135 + 3 + 10;   // lịch mới: sau lần đọc trễ + 1 chu kỳ
    s.run_due();
    CHECK_EQ(dev.stamps.size(), before + 2);
}

TEST(sensor_scheduler_fault_and_recovery_events) {
    g_clock = 0;
    msg::EventBus bus;
    mock::Recorder rec;
    bus.subscribe(msg::evt::kAny, &rec);
    FakeSensor dev;
    msg::Topic<std::int32_t> t;
    FakeTask task("baro", dev, t, 10);
    svc::SensorScheduler s(fake_cfg(3));
    s.add(&task);
    s.set_event_bus(&bus);   // chạy bằng run_due() với đồng hồ giả, không cần thread

    run_until(s, 20);   // 3 mẫu tốt: 0,10,20
    std::int32_t good = 0;
    CHECK(core::is_ok(t.read(good)));
    const std::uint32_t seq_good = t.sequence();

    dev.fail = true;
    run_until(s, 60);   // 30,40,50,60: lỗi -> fault sau lần thứ 3 (50), không lặp lại ở 60
    CHECK_EQ(rec.count(svc::evt::kSensorFault), 1u);
    CHECK_EQ(rec.count(svc::evt::kSensorRecovered), 0u);
    svc::SensorStatusEvent ev{};
    for (const auto& e : rec.events) {
        if (e.id == svc::evt::kSensorFault) CHECK(e.get(ev));
    }
    CHECK(std::string(ev.name) == "baro" && ev.last_error == core::Status::Timeout);
    std::int32_t v = 0;
    CHECK(core::is_ok(t.read(v)) && v == good);   // topic giữ giá trị tốt cuối cùng
    CHECK_EQ(t.sequence(), seq_good);             // không có mẫu mới khi lỗi
    svc::SensorHealth h;
    CHECK(s.health(0, h) && h.faulted && h.errors == 4 && h.consecutive == 4 && h.last_error == core::Status::Timeout);

    dev.fail = false;
    run_until(s, 70);
    CHECK_EQ(rec.count(svc::evt::kSensorRecovered), 1u);
    CHECK(s.health(0, h) && !h.faulted && h.consecutive == 0);
}

TEST(sensor_scheduler_validation) {
    FakeSensor dev;
    msg::Topic<std::int32_t> t;
    FakeTask zero("zero", dev, t, 0);
    FakeTask ok("ok", dev, t, 10);
    svc::SensorScheduler s;
    CHECK(s.add(nullptr) == core::Status::InvalidArg);
    CHECK(s.add(&zero) == core::Status::InvalidArg);   // chu kỳ 0
    svc::SensorScheduler empty;
    CHECK(empty.start(osal::ThreadConfig{"e", osal::Priority::Low, 1024, nullptr}, nullptr) == core::Status::InvalidArg);
    for (std::size_t i = 0; i < EP_CFG_MAX_SENSOR_TASKS; ++i) CHECK(core::is_ok(s.add(&ok)));
    CHECK(s.add(&ok) == core::Status::NoMemory);
}

TEST(sensor_task_adapts_interface_method) {
    g_clock = 0;
    mock::TemperatureSensor sensor;   // dùng qua interface device::ITemperatureSensor
    msg::Topic<std::int32_t> t;
    svc::SensorTask<device::ITemperatureSensor, std::int32_t, &device::ITemperatureSensor::read_deci_celsius> task(
        "tmp", sensor, t, 500);
    sensor.value = 315;
    g_clock      = 42;
    CHECK(core::is_ok(task.sample(&fake_clock)));
    std::int32_t v = 0;
    msg::TopicStamp st;
    CHECK(core::is_ok(t.read(v, &st)) && v == 315 && st.timestamp_ms == 42);
    sensor.fail = true;
    g_clock = 43;
    CHECK(task.sample(&fake_clock) == core::Status::Timeout);
    CHECK(core::is_ok(t.read(v, &st)) && v == 315 && st.timestamp_ms == 42);
}

TEST(sensor_hub_real_threads_and_start_failure_cleanup) {
    mock::TemperatureSensor sensor;
    msg::Topic<std::int32_t> t;
    svc::SensorTask<device::ITemperatureSensor, std::int32_t, &device::ITemperatureSensor::read_deci_celsius> task(
        "tmp", sensor, t, 5);
    {
        svc::SensorScheduler group;
        group.add(&task);
        svc::SensorHub hub;
        CHECK(core::is_ok(hub.add(&group, {"snsr", osal::Priority::High, 2048, nullptr})));
        CHECK(core::is_ok(hub.start(nullptr)));
        CHECK(group.running());
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        hub.stop();
        CHECK(!group.running());
        const std::uint32_t n = t.sequence();
        std::printf("    hub: %u samples in ~80 ms at 5 ms period\n", n);
        CHECK(n >= 10 && n <= 18);
    }
    {   // nhóm thứ hai không có task -> start lỗi -> nhóm thứ nhất đã chạy phải được dừng
        svc::SensorScheduler g1, g2;
        g1.add(&task);
        svc::SensorHub hub;
        hub.add(&g1, {"g1", osal::Priority::Normal, 2048, nullptr});
        hub.add(&g2, {"g2", osal::Priority::Normal, 2048, nullptr});
        const std::size_t threads_before = osal::thread_count();
        CHECK(hub.start(nullptr) == core::Status::InvalidArg);
        CHECK(!g1.running());
        CHECK_EQ(osal::thread_count(), threads_before);
    }
}
