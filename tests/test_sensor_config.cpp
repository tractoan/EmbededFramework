// Cấu hình cảm biến áp dụng NGAY hoặc CHỜ NHỊP (SensorConfigurator, ApplyMode), đổi chu kỳ ngay, TMP102 thật (driver) trên bus giả,
// nối tham số -> cấu hình (SensorParamBinding, SensorPeriodBinding).
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "device/tmp102.hpp"
#include "mocks.hpp"
#include "osal/semaphore.hpp"
#include "osal/time.hpp"
#include "services/sensor_param_binding.hpp"
#include "services/sensor_scheduler.hpp"
#include "test_framework.hpp"

namespace {
template <typename Pred>
bool wait_until(Pred p, int timeout_ms = 2000) {
    for (int i = 0; i < timeout_ms; ++i) {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return p();
}
std::uint64_t ms_since(std::chrono::steady_clock::time_point t0) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
}

// Bus I2C có TMP102 @0x48: thanh ghi con trỏ, Temperature (0), Configuration (1, mặc định 0x60A0).
class FakeTmp102Bus final : public hal::II2c {
public:
    core::Status write(std::uint8_t addr, const std::uint8_t* d, std::size_t n, std::uint32_t) override {
        std::lock_guard<std::mutex> l(m_);
        if (fail || addr != 0x48 || n == 0) return core::Status::Error;
        ptr_ = d[0] & 3;
        if (n >= 3) {
            regs_[ptr_] = static_cast<std::uint16_t>((d[1] << 8) | d[2]);
            if (ptr_ == 1) config_writes.fetch_add(1);
        }
        return core::Status::Ok;
    }
    core::Status read(std::uint8_t addr, std::uint8_t* d, std::size_t n, std::uint32_t) override {
        std::lock_guard<std::mutex> l(m_);
        if (fail || addr != 0x48) return core::Status::Error;
        for (std::size_t i = 0; i < n; ++i) d[i] = static_cast<std::uint8_t>(i % 2 == 0 ? regs_[ptr_] >> 8 : regs_[ptr_]);
        return core::Status::Ok;
    }
    core::Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tn, std::uint8_t* rx, std::size_t rn,
                            std::uint32_t t) override {
        const core::Status st = write(addr, tx, tn, t);
        return core::is_ok(st) ? read(addr, rx, rn, t) : st;
    }
    std::uint16_t reg(int i) {
        std::lock_guard<std::mutex> l(m_);
        return regs_[i];
    }
    void set_temp_raw(std::uint16_t v) {
        std::lock_guard<std::mutex> l(m_);
        regs_[0] = v;
    }
    std::atomic<bool> fail{false};
    std::atomic<int> config_writes{0};

private:
    std::mutex m_;
    std::uint16_t regs_[4] = {0x1900, 0x60A0, 0x4B00, 0x5000};   // 25.0 °C, cấu hình mặc định
    std::uint8_t ptr_      = 0;
};

using Cfg     = device::Tmp102Config;
using TmpTask = svc::SensorTask<device::Tmp102, std::int32_t, &device::Tmp102::read_deci_celsius>;
using TmpConf = svc::SensorConfigurator<device::Tmp102, Cfg, &device::Tmp102::apply>;

// Thiết bị giả phát hiện cấu hình chen vào GIỮA một lần đọc.
struct SlowDevice {
    core::Status read(std::int32_t& out) {
        inside.store(true);
        std::this_thread::sleep_for(std::chrono::microseconds(read_us.load()));
        seen.store(value.load());   // cấu hình mà lần đọc này nhìn thấy
        inside.store(false);
        out = ++n;
        return core::Status::Ok;
    }
    core::Status apply(const int& v) {
        if (inside.load()) overlaps.fetch_add(1);
        apply_thread.store(std::this_thread::get_id());
        if (fail_apply.load()) return core::Status::Error;
        if (apply_us.load() > 0) std::this_thread::sleep_for(std::chrono::microseconds(apply_us.load()));
        value = v;
        return core::Status::Ok;
    }
    std::atomic<bool> inside{false}, fail_apply{false};
    std::atomic<std::thread::id> apply_thread{};
    std::atomic<int> seen{-1};
    std::atomic<int> overlaps{0}, value{0}, read_us{2000}, apply_us{0};
    std::int32_t n = 0;
};
using SlowTask = svc::SensorTask<SlowDevice, std::int32_t, &SlowDevice::read>;
using SlowConf = svc::SensorConfigurator<SlowDevice, int, &SlowDevice::apply>;

struct DrdyDevice {   // task chặn: chờ "ngắt" không bao giờ đến
    core::Status read(std::int32_t& out, std::uint32_t timeout_ms) {
        if (!drdy.take(timeout_ms)) return core::Status::Timeout;
        out = 1;
        return core::Status::Ok;
    }
    core::Status apply(const int& v) {
        value = v;
        return core::Status::Ok;
    }
    osal::Semaphore drdy{0, 4};
    std::atomic<int> value{0};
};
}  // namespace

TEST(tmp102_apply_rate_extended_shutdown_and_decoding) {
    FakeTmp102Bus bus;
    device::Tmp102 tmp;
    tmp.init(&bus);
    Cfg c;
    c.rate     = Cfg::Rate::Hz8;
    c.extended = true;
    CHECK(core::is_ok(tmp.apply(c)));
    CHECK_EQ(bus.reg(1), 0x60F0);   // CR=11, AL giữ nguyên, EM=1, byte cao không đổi
    bus.set_temp_raw(0x4B00);       // 150.0 °C ở định dạng 13 bit
    std::int32_t t = 0;
    CHECK(core::is_ok(tmp.read_deci_celsius(t)) && t == 1500);
    c.extended = false;
    c.shutdown = true;
    c.rate     = Cfg::Rate::Hz0_25;
    CHECK(core::is_ok(tmp.apply(c)));
    CHECK_EQ(bus.reg(1), 0x6120);   // SD=1, CR=00, EM=0
    CHECK(core::is_ok(tmp.read_deci_celsius(t)) && t == 750);   // cùng dữ liệu, định dạng 12 bit
    bus.fail = true;
    c.extended = true;
    CHECK(!core::is_ok(tmp.apply(c)));
    CHECK(!tmp.extended());         // lỗi bus: giữ cách giải mã cũ
}

TEST(sensor_config_applies_immediately_not_at_next_tick) {
    FakeTmp102Bus bus;
    device::Tmp102 tmp;
    tmp.init(&bus);
    msg::Topic<std::int32_t> topic;
    TmpTask task("tmp", tmp, topic, 1000);   // lấy mẫu mỗi 1 s
    TmpConf conf(task, tmp);
    svc::SensorScheduler group;
    CHECK(core::is_ok(group.add(&task)));
    CHECK(core::is_ok(group.start({"snsr", osal::Priority::High, 2048, nullptr}, nullptr)));
    CHECK(wait_until([&] { return topic.sequence() == 1; }));   // mẫu đầu, lần kế tiếp sau ~1 s

    Cfg c;
    c.rate = Cfg::Rate::Hz8;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(core::is_ok(conf.configure(c)));    // kết quả đồng bộ
    const std::uint64_t took = ms_since(t0);
    CHECK_EQ(bus.config_writes.load(), 1);    // đã ghi vào chip khi configure() trả về
    CHECK_EQ(topic.sequence(), 1u);           // không phải đợi tới nhịp lấy mẫu kế tiếp
    std::printf("    configure() applied in %lu ms (sample period 1000 ms)\n", static_cast<unsigned long>(took));
    CHECK(took < 100);
    Cfg cur;
    CHECK(conf.current(cur) && cur.rate == Cfg::Rate::Hz8 && conf.applied() == 1);
    group.stop();
}

TEST(sensor_config_never_interleaves_with_a_read) {
    SlowDevice dev;
    msg::Topic<std::int32_t> topic;
    SlowTask task("slow", dev, topic, 1);   // đọc liên tục, mỗi lần 2 ms
    SlowConf conf(task, dev);
    svc::SensorScheduler group;
    group.add(&task);
    CHECK(core::is_ok(group.start({"snsr", osal::Priority::High, 2048, nullptr}, nullptr)));
    CHECK(wait_until([&] { return topic.sequence() > 3; }));
    int ok = 0;
    for (int i = 0; i < 150; ++i) {     // cấu hình rải theo thời gian, rơi vào mọi pha của lần đọc
        ok += core::is_ok(conf.configure(i)) ? 1 : 0;
        std::this_thread::sleep_for(std::chrono::microseconds(700));
    }
    CHECK_EQ(ok, 150);
    CHECK_EQ(dev.overlaps.load(), 0);   // không lần cấu hình nào rơi vào giữa một lần đọc

    // Cấu hình LIÊN TỤC không nghỉ trong 100 ms, mỗi lần ghi chip mất 1 ms (khóa gần như luôn bị giữ):
    // việc lấy mẫu không được bị bỏ đói – unlock_device() phải nhường cho luồng lấy mẫu đang chờ.
    dev.apply_us.store(1000);
    const std::uint32_t before = topic.sequence();
    const auto t0              = std::chrono::steady_clock::now();
    int busy                   = 0;
    while (ms_since(t0) < 100) busy += conf.configure(1) == core::Status::Busy ? 1 : 0;
    const std::uint32_t during = topic.sequence() - before;
    group.stop();
    std::printf("    continuous configure for 100 ms: %u samples still taken (read = 2 ms), %d busy\n", during, busy);
    CHECK(during >= 10);
    CHECK_EQ(busy, 0);
    CHECK_EQ(dev.overlaps.load(), 0);
}

TEST(sensor_config_busy_when_device_held_too_long_and_settle_discard) {
    SlowDevice dev;
    msg::Topic<std::int32_t> topic;
    SlowTask task("slow", dev, topic, 1000);
    SlowConf conf(task, dev, /*settle_samples*/ 2, /*lock_timeout_ms*/ 20);
    dev.read_us = 200000;                                     // một lần đọc "kẹt" 200 ms
    std::thread reader([&] { task.sample(&osal::now_ms); });
    CHECK(wait_until([&] { return dev.inside.load(); }));
    CHECK(conf.configure(7) == core::Status::Busy);           // không chờ quá lock_timeout_ms
    CHECK_EQ(conf.failures(), 1u);
    reader.join();
    dev.read_us = 0;

    const std::uint32_t before = topic.sequence();
    CHECK(core::is_ok(conf.configure(8)));
    CHECK_EQ(dev.value.load(), 8);
    task.sample(&osal::now_ms);
    task.sample(&osal::now_ms);
    CHECK_EQ(topic.sequence(), before);                       // 2 mẫu đầu sau khi đổi cấu hình: bỏ
    task.sample(&osal::now_ms);
    CHECK_EQ(topic.sequence(), before + 1);
}

TEST(sensor_config_reaches_blocking_task_within_a_slice) {
    DrdyDevice dev;
    msg::Topic<std::int32_t> topic;
    svc::BlockingSensorTask<DrdyDevice, std::int32_t, &DrdyDevice::read> task("imu", dev, topic, 500);
    svc::SensorConfigurator<DrdyDevice, int, &DrdyDevice::apply> conf(task, dev);
    svc::SensorScheduler group;
    group.add(&task);
    CHECK(core::is_ok(group.start({"imu", osal::Priority::High, 2048, nullptr}, nullptr)));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));   // task đang chờ dữ liệu (timeout 500 ms)
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(core::is_ok(conf.configure(42)));
    const std::uint64_t took = ms_since(t0);
    std::printf("    configure() on a blocking task (timeout 500 ms) took %lu ms\n", static_cast<unsigned long>(took));
    CHECK(took < 60);                                             // ~ một lát 10 ms, không phải cả timeout
    CHECK_EQ(dev.value.load(), 42);
    dev.drdy.give();
    CHECK(wait_until([&] { return topic.sequence() == 1; }));     // vẫn nhận dữ liệu bình thường
    group.stop();
}

TEST(sensor_period_change_takes_effect_immediately) {
    SlowDevice dev;
    dev.read_us = 0;
    msg::Topic<std::int32_t> topic;
    SlowTask task("p", dev, topic, 1000);
    svc::SensorScheduler group;
    group.add(&task);
    CHECK(core::is_ok(group.start({"snsr", osal::Priority::High, 2048, nullptr}, nullptr)));
    CHECK(wait_until([&] { return topic.sequence() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(core::is_ok(task.set_period_ms(10)));
    CHECK(wait_until([&] { return topic.sequence() >= 2; }, 1000));
    const std::uint64_t first = ms_since(t0);
    CHECK(wait_until([&] { return topic.sequence() >= 8; }, 1000));
    group.stop();
    std::printf("    period 1000 -> 10 ms: next sample after %lu ms\n", static_cast<unsigned long>(first));
    CHECK(first < 25);   // không chờ hết chu kỳ cũ (1 s) hay lát ngủ 50 ms
    CHECK(task.set_period_ms(0) == core::Status::InvalidArg);
    svc::SensorHealth h;
    CHECK(group.health(0, h) && h.period_ms == 10);
}

TEST(sensor_param_binding_applies_saved_and_changed_params_immediately) {
    static const svc::ParamDef defs[] = {
        {0x9001, "tmp.rate", 2, 0, 3, ""},
        {0x9002, "tmp.ext", 0, 0, 1, ""},
        {0x9003, "tmp.period_ms", 500, 10, 60000, "ms"},
    };
    struct Build {
        static Cfg from(const svc::ParamStore& p) {
            Cfg c;
            c.rate     = static_cast<Cfg::Rate>(p.get_or(0x9001, 2));
            c.extended = p.get_or(0x9002, 0) != 0;
            return c;
        }
    };
    mock::Storage nv;
    {   // lần chạy trước: lưu rate = 8 Hz
        svc::ParamStore ps;
        ps.init(&nv, nullptr);
        ps.register_params(defs, 3);
        ps.set(0x9001, 3);
        CHECK(core::is_ok(ps.save()));
    }
    msg::EventBus bus;
    FakeTmp102Bus i2c;
    device::Tmp102 tmp;
    tmp.init(&i2c);
    msg::Topic<std::int32_t> topic;
    TmpTask task("tmp", tmp, topic, 500);
    TmpConf conf(task, tmp);
    svc::ParamStore ps;
    ps.init(&nv, &bus);
    ps.register_params(defs, 3);
    svc::SensorParamBinding<TmpConf, Cfg> bind(conf, ps, 0x9001, 0x9002, &Build::from);
    svc::SensorPeriodBinding period(task, ps, 0x9003);
    CHECK(core::is_ok(bind.attach(bus)) && core::is_ok(period.attach(bus)));

    CHECK(core::is_ok(ps.load()));                         // khởi động: nạp NV -> kParamsLoaded -> áp dụng
    CHECK_EQ(i2c.reg(1) & 0xC0, 0xC0);                     // CR = 8 Hz từ giá trị đã lưu
    CHECK(core::is_ok(bind.last_status()) && bind.applies() == 1);

    CHECK(core::is_ok(ps.set(0x9002, 1)));                 // người vận hành: bật chế độ 13 bit
    CHECK_EQ(i2c.reg(1) & 0x10, 0x10);                     // đã nằm trên chip khi set() trả về
    CHECK(tmp.extended());
    CHECK(core::is_ok(ps.set(0x9003, 50)));                // đổi chu kỳ qua tham số
    CHECK_EQ(task.period_ms(), 50u);
    CHECK(ps.set(0x9001, 9) == core::Status::OutOfRange);  // ParamStore chặn giá trị ngoài miền
    CHECK_EQ(bind.applies(), 2u);

    i2c.fail = true;                                       // bus lỗi: kết quả được báo, cấu hình cũ giữ nguyên
    ps.set(0x9001, 0);
    CHECK(!core::is_ok(bind.last_status()));
    CHECK_EQ(conf.failures(), 1u);
}

// ---------------------------------------------------------------- ApplyMode::NextSample (chờ nhịp)
TEST(sensor_config_next_sample_waits_for_the_tick_and_runs_on_sampler) {
    SlowDevice dev;
    dev.read_us = 0;
    msg::Topic<std::int32_t> topic;
    SlowTask task("t", dev, topic, 200);
    SlowConf conf(task, dev);
    svc::SensorScheduler group;
    group.add(&task);
    CHECK(core::is_ok(group.start({"snsr", osal::Priority::High, 2048, nullptr}, nullptr)));
    CHECK(wait_until([&] { return topic.sequence() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto t0 = std::chrono::steady_clock::now();
    CHECK(core::is_ok(conf.configure(5, svc::ApplyMode::NextSample)));   // chỉ gửi yêu cầu
    CHECK(ms_since(t0) < 5);
    CHECK(conf.pending());
    CHECK_EQ(dev.value.load(), 0);                                       // chưa đụng thiết bị
    CHECK(wait_until([&] { return conf.applied() == 1; }, 1000));
    const std::uint64_t waited = ms_since(t0);
    CHECK(wait_until([&] { return topic.sequence() >= 2; }));
    group.stop();
    std::printf("    NextSample: applied after %lu ms (period 200 ms, requested ~20 ms after a sample)\n",
                static_cast<unsigned long>(waited));
    CHECK(waited >= 100 && waited < 400);          // đúng nhịp kế tiếp, không sớm hơn
    CHECK(!conf.pending());
    CHECK_EQ(dev.value.load(), 5);
    CHECK_EQ(dev.seen.load(), 5);                  // lần đọc ngay sau đó đã thấy cấu hình mới
    CHECK(dev.apply_thread.load() != std::this_thread::get_id());   // ghi trên luồng lấy mẫu
    CHECK(core::is_ok(conf.last_status()));
    CHECK_EQ(dev.overlaps.load(), 0);
}

TEST(sensor_config_next_sample_coalesces_is_superseded_and_settles) {
    SlowDevice dev;
    dev.read_us = 0;
    msg::Topic<std::int32_t> topic;
    SlowTask task("t", dev, topic, 100);
    SlowConf conf(task, dev, /*settle_samples*/ 1);
    // Trước khi scheduler chạy: gửi 3 yêu cầu -> chỉ cái mới nhất được ghi, ở mẫu đầu tiên.
    for (int v = 1; v <= 3; ++v) CHECK(core::is_ok(conf.configure(v, svc::ApplyMode::NextSample)));
    CHECK_EQ(conf.applied(), 0u);
    task.sample(&osal::now_ms);
    CHECK_EQ(conf.applied(), 1u);
    CHECK_EQ(dev.value.load(), 3);
    CHECK_EQ(dev.seen.load(), 3);
    CHECK_EQ(topic.sequence(), 0u);                // settling: mẫu đọc ngay sau khi đổi bị bỏ
    task.sample(&osal::now_ms);
    CHECK_EQ(topic.sequence(), 1u);

    // Immediate sau NextSample: yêu cầu cũ bị thay, không ghi đè lại ở nhịp sau.
    CHECK(core::is_ok(conf.configure(7, svc::ApplyMode::NextSample)));
    CHECK(core::is_ok(conf.configure(9)));
    CHECK(!conf.pending());
    task.sample(&osal::now_ms);
    CHECK_EQ(dev.value.load(), 9);
    CHECK_EQ(conf.applied(), 2u);
    // NextSample sau Immediate: yêu cầu mới hơn vẫn được ghi.
    CHECK(core::is_ok(conf.configure(11)));
    CHECK(core::is_ok(conf.configure(12, svc::ApplyMode::NextSample)));
    task.sample(&osal::now_ms);
    CHECK_EQ(dev.value.load(), 12);

    // Ghi lỗi ở nhịp: báo qua last_status()/failures(), cấu hình hiện hành giữ nguyên, không thử lại mãi.
    dev.fail_apply = true;
    CHECK(core::is_ok(conf.configure(13, svc::ApplyMode::NextSample)));
    task.sample(&osal::now_ms);
    CHECK(conf.last_status() == core::Status::Error);
    CHECK_EQ(conf.failures(), 1u);
    CHECK(!conf.pending());
    int cur = 0;
    CHECK(conf.current(cur) && cur == 12);
    dev.fail_apply = false;

    // NextSample gửi trong lúc một Immediate (gửi trước) đang chờ khóa: Immediate không được hủy yêu cầu mới hơn.
    dev.read_us = 30000;
    std::thread reader([&] { task.sample(&osal::now_ms); });
    CHECK(wait_until([&] { return dev.inside.load(); }));
    std::thread immediate([&] { CHECK(core::is_ok(conf.configure(20))); });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(core::is_ok(conf.configure(21, svc::ApplyMode::NextSample)));
    reader.join();
    immediate.join();
    dev.read_us = 0;
    CHECK_EQ(dev.value.load(), 20);
    CHECK(conf.pending());
    task.sample(&osal::now_ms);
    CHECK_EQ(dev.value.load(), 21);

    // Mỗi task một bộ cấu hình chờ nhịp.
    SlowConf other(task, dev);
    CHECK(other.configure(1, svc::ApplyMode::NextSample) == core::Status::Busy);
    CHECK(core::is_ok(other.configure(1)));        // Immediate vẫn dùng được
}

TEST(sensor_config_next_sample_on_blocking_task_waits_for_the_next_sample_not_a_slice) {
    DrdyDevice dev;
    msg::Topic<std::int32_t> topic;
    svc::BlockingSensorTask<DrdyDevice, std::int32_t, &DrdyDevice::read> task("imu", dev, topic, 500);
    svc::SensorConfigurator<DrdyDevice, int, &DrdyDevice::apply> conf(task, dev);
    svc::SensorScheduler group;
    group.add(&task);
    CHECK(core::is_ok(group.start({"imu", osal::Priority::High, 2048, nullptr}, nullptr)));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));   // đang chờ dữ liệu
    CHECK(core::is_ok(conf.configure(42, svc::ApplyMode::NextSample)));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));   // nhiều lát trôi qua...
    CHECK_EQ(dev.value.load(), 0);                                // ...nhưng chưa sang lần lấy mẫu mới
    dev.drdy.give();                                              // có mẫu -> lần lấy mẫu kế tiếp bắt đầu
    CHECK(wait_until([&] { return topic.sequence() == 1; }));
    CHECK(wait_until([&] { return dev.value.load() == 42; }, 200));
    group.stop();
}

TEST(sensor_param_binding_next_sample_does_not_block_the_publisher) {
    static const svc::ParamDef defs[] = {{0x9101, "slow.mode", 0, 0, 100, ""}};
    struct Build {
        static int from(const svc::ParamStore& p) { return p.get_or(0x9101, 0); }
    };
    mock::Storage nv;
    msg::EventBus bus;
    svc::ParamStore ps;
    ps.init(&nv, &bus);
    ps.register_params(defs, 1);
    SlowDevice dev;
    msg::Topic<std::int32_t> topic;
    SlowTask task("t", dev, topic, 1000);
    SlowConf conf(task, dev);
    svc::SensorParamBinding<SlowConf, int> bind(conf, ps, 0x9101, 0x9101, &Build::from, svc::ApplyMode::NextSample);
    CHECK(core::is_ok(bind.attach(bus)));

    dev.read_us = 200000;                                         // thiết bị đang bận một lần đọc 200 ms
    std::thread reader([&] { task.sample(&osal::now_ms); });
    CHECK(wait_until([&] { return dev.inside.load(); }));
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(core::is_ok(ps.set(0x9101, 4)));                        // CLI / Modbus / MAVLink đổi tham số
    const std::uint64_t took = ms_since(t0);
    std::printf("    param set with NextSample binding while device busy: %lu ms\n", static_cast<unsigned long>(took));
    CHECK(took < 20);                                             // Immediate sẽ chờ 50 ms rồi Busy
    CHECK(core::is_ok(bind.last_status()) && conf.pending());
    reader.join();
    CHECK_EQ(dev.value.load(), 0);
    dev.read_us = 0;
    task.sample(&osal::now_ms);                                   // nhịp kế tiếp
    CHECK_EQ(dev.value.load(), 4);
    CHECK(core::is_ok(conf.last_status()));
}
