// Dịch vụ bring-up: dãy cổng serial (loopback/stream/nối chéo), giải mã IMU theo bảng định dạng, DI/DO.
#include <cmath>

#include "mocks.hpp"
#include "device/digital_io.hpp"
#include "device/hdlc_imu.hpp"
#include "messaging/topic.hpp"
#include "services/sensor_scheduler.hpp"
#include "services/serial_port_bank.hpp"
#include "test_framework.hpp"

namespace {
void le16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); }
void le32(std::uint8_t* p, std::int32_t s) {
    const auto v = static_cast<std::uint32_t>(s);
    for (int k = 0; k < 4; ++k) p[k] = static_cast<std::uint8_t>(v >> (8 * k));
}
std::uint16_t sum16(const std::uint8_t* p, std::size_t n) {
    std::uint32_t s = 0;
    for (std::size_t i = 0; i + 1 < n; i += 2) s += static_cast<std::uint32_t>(p[i] | (p[i + 1] << 8));
    return static_cast<std::uint16_t>(s);
}
// Stack tĩnh cho thread của SerialPortBank trong test: không lấy từ vùng stack dùng chung (cấp một chiều).
alignas(8) std::uint8_t g_port_stacks[10][2048];
svc::SerialPortBank::PortConfig port(hal::IUart* u, svc::SerialMode m, std::int8_t peer, int stack) {
    svc::SerialPortBank::PortConfig c;
    c.uart        = u;
    c.mode        = m;
    c.peer        = peer;
    c.name        = "tser";
    c.stack_bytes = sizeof(g_port_stacks[0]);
    c.stack_mem   = g_port_stacks[stack];
    return c;
}
bool near(float a, double b, double tol) { return std::fabs(static_cast<double>(a) - b) <= tol; }

// Khung "ln200": Δv z = -400 LSB, Δθ z = 229 LSB @400 Hz
std::vector<std::uint8_t> ln200_frame() {
    std::vector<std::uint8_t> f(26, 0);
    le16(&f[4], static_cast<std::uint16_t>(-400));   // Δv z
    le16(&f[10], 229);                               // Δθ z
    le16(&f[12], 0x00A5);                            // status
    le16(&f[24], sum16(f.data(), 24));
    return f;
}
}  // namespace

TEST(serial_bank_loopback_stream_and_pairs) {
    mock::Uart loop, silent(mock::Uart::Mode::Silent), broken(mock::Uart::Mode::OpenFails),
        corrupt(mock::Uart::Mode::Corrupt), gnss(mock::Uart::Mode::Silent), pa(mock::Uart::Mode::Silent),
        pb(mock::Uart::Mode::Silent);
    svc::SerialPortBank bank;
    bank.set_test_timing(5, 10);
    using M = svc::SerialMode;
    CHECK(core::is_ok(bank.add(port(&loop, M::Loopback, -1, 0))));
    CHECK(core::is_ok(bank.add(port(&silent, M::Loopback, -1, 1))));
    CHECK(core::is_ok(bank.add(port(&broken, M::Loopback, -1, 2))));
    CHECK(core::is_ok(bank.add(port(&corrupt, M::Loopback, -1, 3))));
    CHECK(core::is_ok(bank.add(port(&gnss, M::Stream, -1, 4))));
    CHECK(core::is_ok(bank.add(port(nullptr, M::External, -1, 5))));
    CHECK(core::is_ok(bank.add(port(&pa, M::Loopback, 7, 6))));   // cặp nối chéo 6 <-> 7 (mỗi bên tự "im lặng")
    CHECK(core::is_ok(bank.add(port(&pb, M::Loopback, 6, 7))));
    CHECK(bank.set_mode(5, M::Loopback) == core::Status::InvalidArg);
    CHECK(core::is_ok(bank.start()));

    gnss.inject("$GPGGA,1*00\r\n");
    CHECK(mock::wait_until([&] {
        return bank.stats(0).pass >= 3 && bank.stats(1).fail >= 2 && bank.stats(3).fail >= 2 && bank.stats(6).fail >= 1 &&
               bank.stats(7).fail >= 1;
    }, 3000));
    CHECK(bank.stats(0).last == svc::LoopResult::Pass && bank.stats(0).fail == 0);
    CHECK(bank.stats(1).last == svc::LoopResult::Timeout);
    CHECK(bank.stats(2).last == svc::LoopResult::OpenError);
    CHECK(bank.stats(3).last == svc::LoopResult::Mismatch);
    CHECK(bank.stats(5).mode == M::External && bank.stats(5).pass == 0 && bank.stats(5).fail == 0);
    CHECK(bank.stats(6).fail >= 1 && bank.stats(7).fail >= 1);   // cặp không nối -> cả 2 chiều đều lỗi

    // Stream: đọc được dữ liệu thiết bị ngoài, ghi được lệnh, đổi baudrate
    std::uint8_t buf[64];
    CHECK(mock::wait_until([&] { return bank.stats(4).rx_bytes >= 13; }));
    CHECK_EQ(bank.read(4, buf, sizeof(buf)), 13u);
    CHECK(std::memcmp(buf, "$GPGGA", 6) == 0);
    CHECK_EQ(bank.write(4, reinterpret_cast<const std::uint8_t*>("CFG\r\n"), 5), 5u);
    CHECK_EQ(bank.write(0, buf, 3), 0u);   // cổng Loopback không cho ghi
    CHECK(core::is_ok(bank.set_baudrate(4, 9600)));
    CHECK(mock::wait_until([&] { return gnss.baud.load() == 9600u && bank.stats(4).baudrate == 9600u; }));

    // Stream -> Loopback lúc chạy (kịch bản không có máy thu GNSS)
    CHECK(core::is_ok(bank.set_mode(4, M::Loopback)));
    CHECK(mock::wait_until([&] { return bank.stats(4).fail >= 1; }));
    bank.stop();

    // Khung thử: CRC đúng, khác nhau theo seq
    std::uint8_t a[svc::SerialPortBank::kFrameLen], b[svc::SerialPortBank::kFrameLen];
    svc::SerialPortBank::make_test_frame(2, 1, a);
    svc::SerialPortBank::make_test_frame(2, 2, b);
    CHECK(a[0] == 0x55 && a[1] == 0xAA && a[2] == 2 && a[3] == 1);
    CHECK(std::memcmp(a + 4, b + 4, 12) != 0);
    const std::uint16_t crc = utils::crc16_ccitt(a, sizeof(a) - 2);
    CHECK(a[16] == (crc >> 8) && a[17] == (crc & 0xFF));
}

TEST(serial_bank_cross_pair_pass) {
    // Hai cổng nối chéo thật: ghi trên cổng này xuất hiện ở cổng kia
    struct Cross final : hal::IUart {
        Cross* peer = nullptr;
        mock::Uart rx_side{mock::Uart::Mode::Silent};
        core::Status open(const hal::UartConfig&) override { return core::Status::Ok; }
        void close() override {}
        std::size_t write(const std::uint8_t* d, std::size_t n, std::uint32_t) override {
            peer->rx_side.inject(std::string(reinterpret_cast<const char*>(d), n));
            return n;
        }
        std::size_t read(std::uint8_t* d, std::size_t n, std::uint32_t t) override { return rx_side.read(d, n, t); }
    } a, b;
    a.peer = &b;
    b.peer = &a;
    svc::SerialPortBank bank;
    bank.set_test_timing(5, 20);
    bank.add(port(&a, svc::SerialMode::Loopback, 1, 8));
    bank.add(port(&b, svc::SerialMode::Loopback, 0, 9));
    bank.start();
    CHECK(mock::wait_until([&] { return bank.stats(0).pass >= 3 && bank.stats(1).pass >= 3; }, 3000));
    bank.stop();
    CHECK_EQ(bank.stats(0).fail + bank.stats(1).fail, 0u);
}

TEST(imu_decode_formats) {
    device::ImuSample s{};
    auto f = ln200_frame();
    CHECK(device::decode_imu_frame(device::imu_formats::kLn200, f.data(), f.size(), s) == device::ImuDecodeResult::Ok);
    CHECK(near(s.accel[2], -400.0 / 16384.0 * 400.0, 1e-4));            // -9.7656 m/s²
    CHECK(near(s.gyro[2], 229.0 / 524288.0 * 400.0, 1e-6));            // 0.1747 rad/s
    CHECK(near(s.gyro[0], 0.0, 1e-9) && s.status == 0x00A5 && std::isnan(s.temperature));
    CHECK_EQ(s.raw_len, 26);

    f[3] ^= 1;   // sai checksum
    CHECK(device::decode_imu_frame(device::imu_formats::kLn200, f.data(), f.size(), s) == device::ImuDecodeResult::BadChecksum);
    CHECK(device::decode_imu_frame(device::imu_formats::kLn200, f.data(), 20, s) == device::ImuDecodeResult::BadLength);

    // "hg1700": byte nhận dạng 0x0E/0x01, int32, checksum từ byte 2
    std::vector<std::uint8_t> h(30, 0);
    h[0] = 0x0E; h[1] = 0x01;
    le32(&h[10], 14990000);   // Δθ z
    le32(&h[22], -43180000);  // Δv z (ft/s * 2^27)
    le16(&h[28], sum16(&h[2], 26));
    CHECK(device::decode_imu_frame(device::imu_formats::kHg1700, h.data(), h.size(), s) == device::ImuDecodeResult::Ok);
    CHECK(near(s.gyro[2], 14990000 * std::ldexp(1.0, -33) * 100.0, 1e-6));
    CHECK(near(s.accel[2], -43180000 * std::ldexp(1.0, -27) * 0.3048 * 100.0, 1e-3));
    h[1] = 0x02;   // bản tin khác (vd. bản tin điều khiển) -> không phải dữ liệu
    CHECK(device::decode_imu_frame(device::imu_formats::kHg1700, h.data(), h.size(), s) == device::ImuDecodeResult::NotDataMessage);
}

namespace {
// Cổng HDLC giả: trả lần lượt các khung đã nạp, hết thì trả 0 (như hết timeout).
struct FakeHdlc final : hal::IHdlcPort {
    std::deque<std::vector<std::uint8_t>> frames;
    bool fail_open = false;
    int opens = 0, reads = 0;
    core::Status open(const hal::HdlcConfig&) override {
        ++opens;
        return fail_open ? core::Status::NotFound : core::Status::Ok;
    }
    void close() override {}
    std::size_t read_frame(std::uint8_t* buf, std::size_t cap, std::uint32_t, hal::HdlcFrameStatus* st) override {
        ++reads;
        if (st != nullptr) *st = hal::HdlcFrameStatus{};
        if (frames.empty()) return 0;
        const auto f = frames.front();
        frames.pop_front();
        const std::size_t n = f.size() < cap ? f.size() : cap;
        std::memcpy(buf, f.data(), n);
        return n;
    }
    core::Status write_frame(const std::uint8_t*, std::size_t, std::uint32_t) override { return core::Status::Ok; }
};
std::uint64_t g_fake_ms = 0;
std::uint64_t fake_clock() { return g_fake_ms; }
}  // namespace

TEST(hdlc_imu_driver_counters_and_software_fcs) {
    device::HdlcImu imu;
    device::HdlcImu::Config c;
    c.format       = &device::imu_formats::kLn200;
    c.software_fcs = true;
    CHECK(imu.init(nullptr, c) == core::Status::NotSupported);   // board không có cổng
    CHECK(std::string(imu.format_name()) == "ln200");
    CHECK(imu.open() == core::Status::NotSupported && imu.status() == core::Status::NotSupported);
    device::ImuSample s{};
    CHECK(imu.read(s, 10) == core::Status::NotSupported);

    auto f = ln200_frame();
    const std::uint16_t fcs = utils::crc16_x25(f.data(), f.size());
    f.push_back(static_cast<std::uint8_t>(fcs));
    f.push_back(static_cast<std::uint8_t>(fcs >> 8));
    hal::HdlcFrameStatus ok{}, bad{};
    bad.crc_error = true;

    std::uint64_t t = 1000;
    for (int i = 0; i < 400; ++i) CHECK(imu.process_frame(f.data(), f.size(), ok, t += 2, s));   // 500 Hz trong 0.8 s
    CHECK(!imu.process_frame(f.data(), f.size(), bad, t, s));                          // FCS lỗi do phần cứng báo
    auto g = f;
    g[0] ^= 0xFF;                                                                      // FCS phần mềm sai
    CHECK(!imu.process_frame(g.data(), g.size(), ok, t, s));
    CHECK(!imu.process_frame(f.data(), f.size() - 3, ok, t, s));                       // ngắn -> FCS sai
    imu.process_frame(nullptr, 0, ok, t + 300, s);                                     // hết cửa sổ 1 s
    const device::ImuStats st = imu.stats();
    CHECK_EQ(st.frames, 400u);
    CHECK_EQ(st.crc_errors, 3u);
    CHECK(st.rate_hz >= 350 && st.rate_hz <= 450);
    CHECK_EQ(s.status, 0x00A5);
}

TEST(hdlc_imu_blocking_read_with_sensor_scheduler) {
    FakeHdlc port;
    device::HdlcImu imu;
    device::HdlcImu::Config c;
    c.format = &device::imu_formats::kLn200;
    CHECK(core::is_ok(imu.init(&port, c)) && core::is_ok(imu.open()));

    // read(): bỏ qua khung không hợp lệ, trả mẫu hợp lệ đầu tiên; không có khung -> Timeout
    auto bad = ln200_frame();
    bad[0] ^= 1;   // sai checksum
    port.frames = {bad, ln200_frame()};
    device::ImuSample s{};
    CHECK(imu.read(s, 50) == core::Status::Ok && s.status == 0x00A5);
    CHECK(imu.read(s, 0) == core::Status::Timeout);
    CHECK_EQ(imu.stats().format_errors, 1u);

    // Nhóm SensorScheduler chế độ Blocking (đồng hồ giả, gọi run_due() trực tiếp – không thread)
    msg::Topic<device::ImuSample> topic;
    svc::BlockingSensorTask<device::IImu, device::ImuSample, &device::IImu::read> task("imu0", imu, topic, 100);
    svc::SensorScheduler::Config sc;
    sc.clock = &fake_clock;
    svc::SensorScheduler group(sc);
    msg::EventBus bus;
    mock::Recorder rec;
    bus.subscribe(msg::evt::kAny, &rec);
    group.set_event_bus(&bus);
    CHECK(core::is_ok(group.add(&task)) && group.blocking());

    port.frames = {ln200_frame()};
    g_fake_ms   = 5000;
    group.run_due();
    device::ImuSample got{};
    msg::TopicStamp stamp;
    CHECK(core::is_ok(topic.read(got, &stamp)) && stamp.sequence == 1 && got.status == 0x00A5);
    for (int k = 0; k < 3; ++k) group.run_due();   // 3 lần không có khung -> kSensorFault
    CHECK_EQ(rec.count(svc::evt::kSensorFault), 1u);
    port.frames = {ln200_frame()};
    group.run_due();
    CHECK_EQ(rec.count(svc::evt::kSensorRecovered), 1u);
    svc::SensorHealth h;
    CHECK(group.health(0, h) && h.samples_ok == 2 && h.errors == 3 && !h.faulted);
}

TEST(digital_io_banks) {
    mock::Gpio o[3], i[3];
    device::GpioOutputBank out;
    device::GpioInputBank in;
    for (auto& p : o) out.add(&p);
    for (auto& p : i) in.add(&p);
    CHECK(out.add(nullptr) == core::Status::InvalidArg);
    CHECK(core::is_ok(out.write(0x5, 0x5)));
    CHECK(o[0].read() && !o[1].read() && o[2].read());
    CHECK(core::is_ok(out.write(0x1, 0x0)));   // chỉ kênh 0 đổi
    CHECK_EQ(out.state(), 0x4u);
    CHECK(out.write(0x8, 0x8) == core::Status::OutOfRange);
    i[1].write(true);
    CHECK_EQ(in.read(), 0x2u);
    in.set_invert_mask(0x3);
    CHECK_EQ(in.read(), 0x1u);
}
