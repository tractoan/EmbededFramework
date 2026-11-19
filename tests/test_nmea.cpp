// Bộ giải mã NMEA 0183 (device::NmeaParser): câu mẫu kinh điển của chuẩn, nhiều talker, bán cầu Nam/Tây,
// checksum sai, rác/nhị phân xen giữa, câu bị chia nhỏ.
#include <cstdio>
#include <string>
#include <vector>

#include "device/nmea.hpp"
#include "test_framework.hpp"

namespace {
// "$" + body + "*hh\r\n"
std::string sentence(const std::string& body) {
    unsigned cs = 0;
    for (char c : body) cs ^= static_cast<unsigned char>(c);
    char tail[8];
    std::snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
    return "$" + body + tail;
}
std::vector<device::NmeaParser::Result> feed(device::NmeaParser& p, const std::string& s) {
    std::vector<device::NmeaParser::Result> out;
    for (char c : s) {
        const auto r = p.feed(c);
        if (r != device::NmeaParser::Result::None) out.push_back(r);
    }
    return out;
}
using R = device::NmeaParser::Result;
using F = device::GnssFix;
}  // namespace

TEST(nmea_gga_rmc_reference_sentences) {
    device::NmeaParser p;
    CHECK(feed(p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n") == std::vector<R>{R::Gga});
    F f = p.fix();
    CHECK_EQ(f.time_ms, 45319000u);   // 12:35:19
    CHECK_EQ(f.lat_e7, 481173000);    // 48° 07.038'
    CHECK_EQ(f.lon_e7, 115166667);    // 11° 31.000'
    CHECK_EQ(f.alt_mm, 545400);
    CHECK_EQ(f.hdop_x100, 90);
    CHECK(f.quality == 1 && f.satellites == 8);
    CHECK(f.flags == (F::kTimeValid | F::kPositionValid | F::kAltitudeValid));
    CHECK_EQ(f.unix_time_us(), 0u);   // chưa có ngày

    CHECK(feed(p, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n") == std::vector<R>{R::Rmc});
    f = p.fix();
    CHECK(f.year == 1994 && f.month == 3 && f.day == 23);
    CHECK_EQ(f.speed_cms, 1152);      // 22.4 kn
    CHECK_EQ(f.cog_cdeg, 8440);
    CHECK(f.flags == (F::kTimeValid | F::kDateValid | F::kPositionValid | F::kAltitudeValid));
    CHECK_EQ(f.unix_time_us(), 764426119000000ull);
    CHECK_EQ(p.sentences(), 2u);
}

TEST(nmea_hemispheres_talkers_and_invalid_fix) {
    device::NmeaParser p;
    // GNSS đa hệ (GN), bán cầu Nam/Tây, giờ có phần thập phân, độ cao âm
    CHECK(feed(p, sentence("GNGGA,235959.25,3351.1234567,S,15112.5000000,W,2,14,0.65,-12.3,M,,M,,")) ==
          std::vector<R>{R::Gga});
    F f = p.fix();
    CHECK_EQ(f.time_ms, 86399250u);
    CHECK_EQ(f.lat_e7, -338520576);    // -(33 + 51.1234567/60)
    CHECK_EQ(f.lon_e7, -1512083333);   // -(151 + 12.5/60)
    CHECK_EQ(f.alt_mm, -12300);
    CHECK(f.quality == 2 && f.satellites == 14 && f.hdop_x100 == 65);

    // Không fix: giữ giờ, bỏ vị trí
    CHECK(feed(p, sentence("GPGGA,000001.00,,,,,0,00,99.99,,,,,,")) == std::vector<R>{R::Gga});
    f = p.fix();
    CHECK(f.flags == F::kTimeValid && f.quality == 0 && f.time_ms == 1000);
    // RMC trạng thái V (cảnh báo): có ngày/giờ, không vị trí
    CHECK(feed(p, sentence("GLRMC,000002.00,V,,,,,,,010125,,,N")) == std::vector<R>{R::Rmc});
    f = p.fix();
    CHECK(f.flags == (F::kTimeValid | F::kDateValid) && f.year == 2025 && f.day == 1);
    CHECK(f.speed_cms == F::kUnknown16 && f.cog_cdeg == F::kUnknown16);

    // Câu khác loại -> OtherSentence (không đổi fix)
    CHECK(feed(p, sentence("GPGSV,1,1,00")) == std::vector<R>{R::OtherSentence});
}

TEST(nmea_robustness) {
    device::NmeaParser p;
    const std::string good = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    std::string bad        = good;
    bad[20]                = '9';   // sai checksum
    CHECK(feed(p, bad) == std::vector<R>{R::ChecksumError});
    CHECK_EQ(p.checksum_errors(), 1u);
    CHECK(feed(p, "$GPGGA,123519,4807.038,N\r\n") == std::vector<R>{R::Malformed});   // không có checksum

    // Rác nhị phân (vd. UBX) xen giữa, câu bắt đầu lại bằng '$', câu bị chia thành từng byte
    std::string mixed = std::string("\xB5\x62\x01\x07\x00", 5) + "$GPG" + std::string("\x01\x02", 2) + good;
    std::vector<R> all;
    for (char c : mixed) {
        const auto r = p.feed(c);
        if (r != R::None) all.push_back(r);
    }
    CHECK(!all.empty() && all.back() == R::Gga);
    CHECK_EQ(p.fix().lat_e7, 481173000);

    // Câu quá dài -> bỏ, không tràn bộ đệm
    CHECK(feed(p, "$GP" + std::string(200, 'A') + "\r\n").front() == R::Malformed);

    // Trường sai miền
    std::int32_t e7 = 0;
    CHECK(!device::nmea::parse_coord("9100.000", "N", true, e7));    // > 90°
    CHECK(!device::nmea::parse_coord("4807.038", "E", true, e7));    // bán cầu không hợp lệ cho vĩ độ
    std::uint32_t ms = 0;
    CHECK(!device::nmea::parse_time("246000", ms));
    std::uint16_t y = 0;
    std::uint8_t m = 0, d = 0;
    CHECK(!device::nmea::parse_date("321399", y, m, d));
}

// ---- NmeaFixParser: một fix mỗi epoch; dùng làm Parser của svc::StreamSensorTask
#include "messaging/topic.hpp"
#include "services/sensor_scheduler.hpp"

namespace {
int fixes_from(device::NmeaFixParser& p, const std::string& s, F* last = nullptr) {
    int n = 0;
    for (char c : s) {
        F f;
        if (p.feed(static_cast<std::uint8_t>(c), f)) {
            ++n;
            if (last != nullptr) *last = f;
        }
    }
    return n;
}
const std::string kGga = sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
const std::string kRmc = sentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
const std::string kGsv = sentence("GPGSV,1,1,00");

// UART giả không chờ: trả byte đang có, hết thì trả 0 ngay.
class QueueUart final : public hal::IUart {
public:
    core::Status open(const hal::UartConfig&) override { return core::Status::Ok; }
    void close() override {}
    std::size_t write(const std::uint8_t*, std::size_t len, std::uint32_t) override { return len; }
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t) override {
        std::size_t n = 0;
        while (n < len && pos < q.size()) data[n++] = static_cast<std::uint8_t>(q[pos++]);
        return n;
    }
    std::string q;
    std::size_t pos = 0;
};
std::uint64_t g_ms = 0;
std::uint64_t clock_ms() { return g_ms; }
}  // namespace

TEST(nmea_fix_parser_one_fix_per_epoch) {
    device::NmeaFixParser p;
    F f;
    CHECK_EQ(fixes_from(p, kRmc, &f), 1);                   // chưa từng thấy GGA: RMC ra fix
    CHECK((f.flags & F::kDateValid) && f.year == 1994);
    CHECK_EQ(fixes_from(p, kGga, &f), 1);                   // GGA kết thúc epoch, giữ ngày từ RMC
    CHECK(f.alt_mm == 545400 && f.year == 1994 && f.satellites == 8);
    CHECK_EQ(fixes_from(p, kRmc + kGsv + kGga + kRmc + kGga), 2);   // máy thu phát cả hai: chỉ theo GGA
    CHECK_EQ(fixes_from(p, kRmc), 0);                       // GGA bị tắt: chậm một epoch...
    CHECK_EQ(fixes_from(p, kRmc + kRmc), 2);                // ...rồi mỗi RMC một fix
    CHECK_EQ(fixes_from(p, kGga + kRmc), 1);                // GGA quay lại: lại theo GGA
    CHECK(p.last_result() == R::Rmc && p.nmea().sentences() == 12 && p.fixes() == 7);
    CHECK_EQ(fixes_from(p, "$GPGGA,bad*00\r\n"), 0);
    CHECK(p.last_result() == R::ChecksumError);
    p.reset();
    CHECK_EQ(fixes_from(p, kRmc), 1);                       // reset: lại như chưa thấy GGA
}

TEST(nmea_fix_parser_as_stream_sensor_task) {
    QueueUart uart;
    device::NmeaFixParser nmea;
    msg::Topic<F> topic;
    svc::StreamSensorTask<device::NmeaFixParser, F> task("gnss", uart, nmea, topic, 1000);
    g_ms = 100;
    uart.q = kGga.substr(0, 20);                            // câu bị cắt giữa hai lần đọc
    CHECK(task.sample(&clock_ms) == core::Status::Timeout);
    uart.q += kGga.substr(20) + kRmc + kGga;
    F f;
    msg::TopicStamp st;
    CHECK(core::is_ok(task.sample(&clock_ms)));             // dừng ở fix đầu tiên
    CHECK(core::is_ok(topic.read(f, &st)) && st.sequence == 1 && f.lat_e7 == 481173000 && f.quality == 1);
    CHECK(core::is_ok(task.sample(&clock_ms)));             // phần còn lại (RMC + GGA) -> một fix
    CHECK(core::is_ok(topic.read(f, &st)) && st.sequence == 2 && f.year == 1994);
    CHECK(task.sample(&clock_ms) == core::Status::Timeout);   // hết dữ liệu: lỗi -> sức khỏe của scheduler
}
