// Dịch vụ bring-up: dãy cổng serial (loopback/stream/nối chéo), giải mã IMU theo bảng định dạng, DI/DO.
#include <cmath>

#include "mocks.hpp"
#include "services/drivers/digital_io.hpp"
#include "services/drivers/hdlc_imu.hpp"
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
    CHECK(core::is_ok(bank.add({&loop, {}, M::Loopback, -1, "t0"})));
    CHECK(core::is_ok(bank.add({&silent, {}, M::Loopback, -1, "t1"})));
    CHECK(core::is_ok(bank.add({&broken, {}, M::Loopback, -1, "t2"})));
    CHECK(core::is_ok(bank.add({&corrupt, {}, M::Loopback, -1, "t3"})));
    CHECK(core::is_ok(bank.add({&gnss, {}, M::Stream, -1, "t4"})));
    CHECK(core::is_ok(bank.add({nullptr, {}, M::External, -1, "t5"})));
    CHECK(core::is_ok(bank.add({&pa, {}, M::Loopback, 7, "t6"})));   // cặp nối chéo 6 <-> 7 (mỗi bên tự "im lặng")
    CHECK(core::is_ok(bank.add({&pb, {}, M::Loopback, 6, "t7"})));
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
    bank.add({&a, {}, svc::SerialMode::Loopback, 1, "a"});
    bank.add({&b, {}, svc::SerialMode::Loopback, 0, "b"});
    bank.start();
    CHECK(mock::wait_until([&] { return bank.stats(0).pass >= 3 && bank.stats(1).pass >= 3; }, 3000));
    bank.stop();
    CHECK_EQ(bank.stats(0).fail + bank.stats(1).fail, 0u);
}

TEST(imu_decode_formats) {
    svc::ImuSample s;
    auto f = ln200_frame();
    CHECK(svc::decode_imu_frame(svc::imu_formats::kLn200, f.data(), f.size(), s) == svc::ImuDecodeResult::Ok);
    CHECK(near(s.accel[2], -400.0 / 16384.0 * 400.0, 1e-4));            // -9.7656 m/s²
    CHECK(near(s.gyro[2], 229.0 / 524288.0 * 400.0, 1e-6));            // 0.1747 rad/s
    CHECK(near(s.gyro[0], 0.0, 1e-9) && s.status == 0x00A5 && std::isnan(s.temperature));
    CHECK_EQ(s.raw_len, 26);

    f[3] ^= 1;   // sai checksum
    CHECK(svc::decode_imu_frame(svc::imu_formats::kLn200, f.data(), f.size(), s) == svc::ImuDecodeResult::BadChecksum);
    CHECK(svc::decode_imu_frame(svc::imu_formats::kLn200, f.data(), 20, s) == svc::ImuDecodeResult::BadLength);

    // "hg1700": byte nhận dạng 0x0E/0x01, int32, checksum từ byte 2
    std::vector<std::uint8_t> h(30, 0);
    h[0] = 0x0E; h[1] = 0x01;
    le32(&h[10], 14990000);   // Δθ z
    le32(&h[22], -43180000);  // Δv z (ft/s * 2^27)
    le16(&h[28], sum16(&h[2], 26));
    CHECK(svc::decode_imu_frame(svc::imu_formats::kHg1700, h.data(), h.size(), s) == svc::ImuDecodeResult::Ok);
    CHECK(near(s.gyro[2], 14990000 * std::ldexp(1.0, -33) * 100.0, 1e-6));
    CHECK(near(s.accel[2], -43180000 * std::ldexp(1.0, -27) * 0.3048 * 100.0, 1e-3));
    h[1] = 0x02;   // bản tin khác (vd. bản tin điều khiển) -> không phải dữ liệu
    CHECK(svc::decode_imu_frame(svc::imu_formats::kHg1700, h.data(), h.size(), s) == svc::ImuDecodeResult::NotDataMessage);
}

TEST(hdlc_imu_driver_counters_and_software_fcs) {
    svc::HdlcImu imu;
    svc::HdlcImu::Config c;
    c.format       = &svc::imu_formats::kLn200;
    c.software_fcs = true;
    CHECK(imu.init(nullptr, c) == core::Status::NotSupported);   // board không có cổng
    CHECK(std::string(imu.format_name()) == "ln200");
    CHECK(imu.start() == core::Status::NotSupported);

    auto f = ln200_frame();
    const std::uint16_t fcs = utils::crc16_x25(f.data(), f.size());
    f.push_back(static_cast<std::uint8_t>(fcs));
    f.push_back(static_cast<std::uint8_t>(fcs >> 8));
    hal::HdlcFrameStatus ok{}, bad{};
    bad.crc_error = true;

    std::uint64_t t = 1000;
    for (int i = 0; i < 400; ++i) imu.process_frame(f.data(), f.size(), ok, t += 2);   // 500 Hz trong 0.8 s
    imu.process_frame(f.data(), f.size(), bad, t);                                  // FCS lỗi do phần cứng báo
    auto g = f;
    g[0] ^= 0xFF;                                                                   // FCS phần mềm sai
    imu.process_frame(g.data(), g.size(), ok, t);
    imu.process_frame(f.data(), f.size() - 3, ok, t);                               // ngắn -> FCS sai
    imu.process_frame(nullptr, 0, ok, t + 300);                                     // hết cửa sổ 1 s
    const svc::ImuStats st = imu.stats();
    CHECK_EQ(st.frames, 400u);
    CHECK_EQ(st.crc_errors, 3u);
    CHECK(st.rate_hz >= 350 && st.rate_hz <= 450);
    svc::ImuSample s;
    CHECK(imu.latest(s) && s.status == 0x00A5);
}

TEST(digital_io_banks) {
    mock::Gpio o[3], i[3];
    svc::GpioOutputBank out;
    svc::GpioInputBank in;
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
