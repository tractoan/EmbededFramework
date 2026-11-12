// Lấy mẫu kiểu chặn: BlockingSensorTask (driver chờ DRDY) và StreamSensorTask (dòng byte UART + parser).
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "mocks.hpp"
#include "osal/semaphore.hpp"
#include "osal/time.hpp"
#include "services/sensor_scheduler.hpp"
#include "test_framework.hpp"

namespace {
// UART giả: test đẩy byte vào; read() chờ tối đa timeout như UART thật. closed = trả 0 ngay (không chờ).
class FakeUart final : public hal::IUart {
public:
    core::Status open(const hal::UartConfig&) override { return core::Status::Ok; }
    void close() override {}
    std::size_t write(const std::uint8_t*, std::size_t len, std::uint32_t) override { return len; }
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        std::unique_lock<std::mutex> lk(m_);
        if (closed) return 0;
        cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return !q_.empty(); });
        std::size_t n = 0;
        while (n < len && !q_.empty()) {
            data[n++] = q_.front();
            q_.pop_front();
        }
        return n;
    }
    void push(const char* text) {
        {
            std::lock_guard<std::mutex> lk(m_);
            for (const char* p = text; *p; ++p) q_.push_back(static_cast<std::uint8_t>(*p));
        }
        cv_.notify_all();
    }
    bool closed = false;

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::uint8_t> q_;
};

// Parser giả kiểu NMEA tối giản: "$<số>\n" -> mẫu; ký tự lạ -> bỏ, chờ '$' kế tiếp (đồng bộ lại).
class LineParser {
public:
    bool feed(std::uint8_t b, std::int32_t& out) {
        if (b == '$') {
            in_ = true;
            value_ = 0;
            digits_ = 0;
            return false;
        }
        if (!in_) return false;
        if (b >= '0' && b <= '9' && digits_ < 9) {
            value_ = value_ * 10 + (b - '0');
            ++digits_;
            return false;
        }
        const bool ok = b == '\n' && digits_ > 0;
        in_           = false;
        if (ok) out = value_;
        return ok;
    }

private:
    bool in_ = false;
    std::int32_t value_ = 0;
    int digits_ = 0;
};
using LineTask = svc::StreamSensorTask<LineParser, std::int32_t>;

// Driver chờ ngắt "dữ liệu sẵn sàng": ISR give_from_isr(), read() chờ semaphore với timeout.
class DrdySensor {
public:
    core::Status read(std::int32_t& out, std::uint32_t timeout_ms) {
        ++calls;
        if (fail_fast) return core::Status::Error;   // lỗi trả về ngay, không chờ
        if (!drdy.take(timeout_ms)) return core::Status::Timeout;
        out = value.load();
        return core::Status::Ok;
    }
    osal::Semaphore drdy{0, 16};
    std::atomic<std::int32_t> value{0};
    std::atomic<int> calls{0};
    std::atomic<bool> fail_fast{false};
};
using DrdyTask = svc::BlockingSensorTask<DrdySensor, std::int32_t, &DrdySensor::read>;

class Periodic final : public svc::ISensorTask {
public:
    const char* name() const override { return "p"; }
    std::uint32_t period_ms() const override { return 10; }
    core::Status sample(svc::SensorClock) override { return core::Status::Ok; }
};

template <typename Pred>
bool wait_until(Pred p, int timeout_ms = 2000) {
    for (int i = 0; i < timeout_ms; ++i) {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return p();
}
svc::SensorClock real_clock() { return &osal::now_ms; }
}  // namespace

TEST(sensor_blocking_task_must_own_its_group) {
    DrdySensor dev;
    msg::Topic<std::int32_t> t;
    DrdyTask a("a", dev, t, 20), b("b", dev, t, 20);
    Periodic p;
    {
        svc::SensorScheduler g;
        CHECK(core::is_ok(g.add(&a)));
        CHECK(g.blocking());
        CHECK(g.add(&p) == core::Status::InvalidArg);   // định kỳ sau chặn
        CHECK(g.add(&b) == core::Status::InvalidArg);   // hai task chặn
    }
    {
        svc::SensorScheduler g;
        CHECK(core::is_ok(g.add(&p)));
        CHECK(!g.blocking());
        CHECK(g.add(&a) == core::Status::InvalidArg);   // chặn sau định kỳ
    }
}

TEST(sensor_stream_task_parses_across_reads_and_resyncs) {
    FakeUart uart;
    LineParser parser;
    msg::Topic<std::int32_t> t;
    LineTask task("gnss", uart, parser, t, 30);
    CHECK(task.mode() == svc::SamplingMode::Blocking);
    std::int32_t v = 0;
    msg::TopicStamp st;

    uart.push("$12");                                   // mẫu bị cắt giữa hai lần đọc
    CHECK(task.sample(real_clock()) == core::Status::Timeout);
    CHECK(t.read(v) == core::Status::NotFound);
    uart.push("34\n");
    CHECK(core::is_ok(task.sample(real_clock())));
    CHECK(core::is_ok(t.read(v, &st)) && v == 1234 && st.sequence == 1);

    uart.push("$1\n$2\n$3\n");                         // nhiều mẫu trong một lần đọc: publish từng mẫu
    CHECK(core::is_ok(task.sample(real_clock())));
    CHECK(core::is_ok(t.read(v, &st)) && v == 3 && st.sequence == 4);

    uart.push("xx$9z$7\n");                             // rác + mẫu hỏng -> đồng bộ lại ở '$' kế tiếp
    CHECK(core::is_ok(task.sample(real_clock())));
    CHECK(core::is_ok(t.read(v, &st)) && v == 7 && st.sequence == 5);

    const std::uint64_t t0 = osal::now_ms();            // không có dữ liệu: Timeout sau ~timeout
    CHECK(task.sample(real_clock()) == core::Status::Timeout);
    const std::uint64_t dt = osal::now_ms() - t0;
    CHECK(dt >= 25 && dt < 300);
    CHECK_EQ(task.bytes(), 3u + 3u + 9u + 8u);
}

TEST(sensor_blocking_drdy_fault_recovery_and_stop_latency) {
    msg::EventBus bus;
    mock::Recorder rec;   // chỉ thread của scheduler ghi; test đọc sau khi đã dừng
    bus.subscribe(msg::evt::kAny, &rec);
    DrdySensor dev;
    msg::Topic<std::int32_t> t;
    DrdyTask task("imu", dev, t, 20);
    svc::SensorScheduler group;
    CHECK(core::is_ok(group.add(&task)));
    CHECK(core::is_ok(group.start({"imu", osal::Priority::High, 2048, nullptr}, &bus)));

    for (int i = 1; i <= 5; ++i) {   // "ngắt DRDY": mỗi lần có dữ liệu, task đọc ngay (không chờ chu kỳ)
        dev.value = i * 100;
        const std::uint64_t given = osal::now_ms();
        dev.drdy.give();
        CHECK(wait_until([&] { return t.sequence() == static_cast<std::uint32_t>(i); }));
        std::int32_t v = 0;
        msg::TopicStamp st;
        CHECK(core::is_ok(t.read(v, &st)) && v == i * 100 && st.timestamp_ms >= given);   // timestamp lúc có dữ liệu
    }
    // Không có dữ liệu: mỗi 20 ms một Timeout -> sau 3 lần liên tiếp: kSensorFault
    svc::SensorHealth h;
    CHECK(wait_until([&] { return group.health(0, h) && h.faulted; }));
    dev.value = 999;
    dev.drdy.give();
    CHECK(wait_until([&] { return group.health(0, h) && !h.faulted; }));

    const std::uint64_t t0 = osal::now_ms();
    group.stop();
    const std::uint64_t stop_ms = osal::now_ms() - t0;
    std::printf("    blocking group stop latency: %lu ms (timeout 20 ms)\n", static_cast<unsigned long>(stop_ms));
    CHECK(stop_ms < 200);
    CHECK(group.health(0, h) && h.samples_ok == 6 && h.errors >= 3 && h.mode == svc::SamplingMode::Blocking);
    CHECK_EQ(rec.count(svc::evt::kSensorFault), 1u);
    CHECK_EQ(rec.count(svc::evt::kSensorRecovered), 1u);
}

TEST(sensor_blocking_fast_failure_backs_off) {
    DrdySensor dev;
    dev.fail_fast = true;   // vd. bus lỗi: read() trả lỗi NGAY thay vì chờ
    msg::Topic<std::int32_t> t;
    // timeout 120 ms > lát ngủ 50 ms: phải nghỉ ĐỦ 120 ms (lần thử ở ~0, 120, 240, 360 ms trong 400 ms)
    DrdyTask task("baro", dev, t, 120);
    svc::SensorScheduler group;
    group.add(&task);
    CHECK(core::is_ok(group.start({"baro", osal::Priority::High, 2048, nullptr}, nullptr)));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    group.stop();
    std::printf("    fast-failing blocking task: %d attempts in 400 ms (timeout 120 ms)\n", dev.calls.load());
    CHECK(dev.calls.load() >= 3 && dev.calls.load() <= 5);   // ~1 lần / timeout, không quay liên tục

    FakeUart uart;          // UART đóng: read() trả 0 ngay -> stream task không được quay bận
    uart.closed = true;
    LineParser parser;
    LineTask stream("gnss", uart, parser, t, 30);
    const std::uint64_t t0 = osal::now_ms();
    CHECK(stream.sample(real_clock()) == core::Status::Timeout);
    CHECK(osal::now_ms() - t0 < 10);
}
