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
