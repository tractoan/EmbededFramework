#include "device/nmea.hpp"

#include <cstring>

namespace device {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Số ngày từ 1970-01-01 tới ngày y-m-d (lịch Gregory) – thuật toán days_from_civil.
std::int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era      = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Tốc độ knot (dạng "12.345") -> cm/s, làm tròn.
bool knots_to_cms(const char* s, std::uint16_t& out) {
    std::int32_t milli_knots = 0;
    if (!nmea::parse_fixed(s, 3, milli_knots) || milli_knots < 0) return false;
    const std::int64_t cms = (static_cast<std::int64_t>(milli_knots) * 5144 + 50000) / 100000;   // 1 kn = 51.444 cm/s
    out = static_cast<std::uint16_t>(cms > 0xFFFE ? 0xFFFE : cms);
    return true;
}

}  // namespace

std::uint64_t GnssFix::unix_time_us() const {
    if (!(flags & kDateValid) || !(flags & kTimeValid)) return 0;
    const std::int64_t days = days_from_civil(year, month, day);
    if (days < 0) return 0;
    return static_cast<std::uint64_t>(days) * 86400000000ull + static_cast<std::uint64_t>(time_ms) * 1000ull;
}

namespace nmea {

bool parse_fixed(const char* s, int decimals, std::int32_t& out) {
    if (s == nullptr || *s == '\0') return false;
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    std::int64_t v = 0;
    int frac = -1;   // -1: chưa gặp dấu chấm
    bool any = false;
    for (; *s; ++s) {
        if (*s == '.') {
            if (frac >= 0) return false;
            frac = 0;
            continue;
        }
        if (!is_digit(*s)) return false;
        any = true;
        if (frac >= 0) {
            if (frac >= decimals) continue;   // bỏ chữ số thừa (cắt, không làm tròn)
            ++frac;
        }
        v = v * 10 + (*s - '0');
        if (v > 0x7FFFFFFFll * 10) return false;
    }
    if (!any) return false;
    for (int k = frac < 0 ? 0 : frac; k < decimals; ++k) v *= 10;
    if (v > 0x7FFFFFFF) return false;
    out = static_cast<std::int32_t>(neg ? -v : v);
    return true;
}

bool parse_time(const char* s, std::uint32_t& ms) {
    if (s == nullptr || std::strlen(s) < 6) return false;
    for (int i = 0; i < 6; ++i) {
        if (!is_digit(s[i])) return false;
    }
    const unsigned h = static_cast<unsigned>((s[0] - '0') * 10 + (s[1] - '0'));
    const unsigned m = static_cast<unsigned>((s[2] - '0') * 10 + (s[3] - '0'));
    const unsigned sec = static_cast<unsigned>((s[4] - '0') * 10 + (s[5] - '0'));
    if (h > 23 || m > 59 || sec > 60) return false;   // 60: giây nhuận
    std::int32_t frac_ms = 0;
    if (s[6] == '.') {
        char buf[8] = {'0', '.'};
        std::size_t k = 2;
        for (const char* p = s + 7; *p && k < sizeof(buf) - 1; ++p) buf[k++] = *p;
        buf[k] = '\0';
        if (!parse_fixed(buf, 3, frac_ms)) return false;
    } else if (s[6] != '\0') {
        return false;
    }
    ms = ((h * 60u + m) * 60u + sec) * 1000u + static_cast<std::uint32_t>(frac_ms);
    return true;
}

bool parse_date(const char* s, std::uint16_t& y, std::uint8_t& m, std::uint8_t& d) {
    if (s == nullptr || std::strlen(s) != 6) return false;
    for (int i = 0; i < 6; ++i) {
        if (!is_digit(s[i])) return false;
    }
    const int dd = (s[0] - '0') * 10 + (s[1] - '0');
    const int mm = (s[2] - '0') * 10 + (s[3] - '0');
    const int yy = (s[4] - '0') * 10 + (s[5] - '0');
    if (dd < 1 || dd > 31 || mm < 1 || mm > 12) return false;
    y = static_cast<std::uint16_t>(yy < 80 ? 2000 + yy : 1900 + yy);
    m = static_cast<std::uint8_t>(mm);
    d = static_cast<std::uint8_t>(dd);
    return true;
}

bool parse_coord(const char* s, const char* hemi, bool is_lat, std::int32_t& e7) {
    if (s == nullptr || hemi == nullptr || *s == '\0') return false;
    const char* dot = std::strchr(s, '.');
    const std::size_t int_len = dot ? static_cast<std::size_t>(dot - s) : std::strlen(s);
    const std::size_t deg_len = is_lat ? 2 : 3;   // ddmm.mmmm / dddmm.mmmm
    if (int_len != deg_len + 2) return false;
    int deg = 0;
    for (std::size_t i = 0; i < deg_len; ++i) {
        if (!is_digit(s[i])) return false;
        deg = deg * 10 + (s[i] - '0');
    }
    std::int32_t min_e7 = 0;   // phút x 1e7
    if (!parse_fixed(s + deg_len, 7, min_e7) || min_e7 >= 600000000) return false;
    const std::int64_t v = static_cast<std::int64_t>(deg) * 10000000 + (static_cast<std::int64_t>(min_e7) + 30) / 60;
    if (v > (is_lat ? 900000000ll : 1800000000ll)) return false;
    char h = hemi[0];
    if (is_lat ? (h != 'N' && h != 'S') : (h != 'E' && h != 'W')) return false;
    e7 = static_cast<std::int32_t>((h == 'S' || h == 'W') ? -v : v);
    return true;
}

}  // namespace nmea

void NmeaParser::reset() {
    len_         = 0;
    in_sentence_ = false;
    fix_         = GnssFix{};
}

NmeaParser::Result NmeaParser::feed(char c) {
    if (c == '$') {   // đầu câu mới (bỏ câu dở dang trước đó)
        in_sentence_ = true;
        len_         = 0;
        return Result::None;
    }
    if (!in_sentence_) return Result::None;
    if (c == '\r' || c == '\n') {
        in_sentence_ = false;
        return finish();
    }
    if (len_ >= kMaxSentence || static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
        in_sentence_ = false;   // quá dài / ký tự lạ (dữ liệu nhị phân xen giữa): bỏ câu
        return Result::Malformed;
    }
    line_[len_++] = c;
    return Result::None;
}

NmeaParser::Result NmeaParser::finish() {
    line_[len_] = '\0';
    char* star  = std::strchr(line_, '*');
    if (star == nullptr || star + 3 != line_ + len_) return Result::Malformed;   // bắt buộc có checksum *hh
    const int hi = hex_value(star[1]), lo = hex_value(star[2]);
    if (hi < 0 || lo < 0) return Result::Malformed;
    std::uint8_t cs = 0;
    for (const char* p = line_; p < star; ++p) cs = static_cast<std::uint8_t>(cs ^ static_cast<std::uint8_t>(*p));
    if (cs != static_cast<std::uint8_t>(hi * 16 + lo)) {
        ++checksum_errors_;
        return Result::ChecksumError;
    }
    ++sentences_;
    *star = '\0';

    // Tách trường tại chỗ (trường rỗng -> chuỗi rỗng)
    char* f[24];
    std::size_t n = 0;
    f[n++] = line_;
    for (char* p = line_; *p && n < sizeof(f) / sizeof(f[0]); ++p) {
        if (*p == ',') {
            *p     = '\0';
            f[n++] = p + 1;
        }
    }
    const std::size_t id_len = std::strlen(f[0]);
    if (id_len != 5) return Result::OtherSentence;   // "GPGGA": 2 ký tự talker + 3 ký tự loại câu
    const char* type = f[0] + 2;
    if (std::strcmp(type, "GGA") == 0) return parse_gga(f, n) ? Result::Gga : Result::Malformed;
    if (std::strcmp(type, "RMC") == 0) return parse_rmc(f, n) ? Result::Rmc : Result::Malformed;
    return Result::OtherSentence;
}

// GGA: 1 time, 2 lat, 3 N/S, 4 lon, 5 E/W, 6 quality, 7 sats, 8 hdop, 9 alt, 10 M, ...
bool NmeaParser::parse_gga(char** f, std::size_t n) {
    if (n < 10) return false;
    GnssFix x = fix_;
    x.flags = static_cast<std::uint8_t>(x.flags & GnssFix::kDateValid);   // ngày giữ từ RMC
    if (nmea::parse_time(f[1], x.time_ms)) x.flags |= GnssFix::kTimeValid;
    std::int32_t q = 0, sats = 0, hdop = 0, alt = 0;
    x.quality    = nmea::parse_fixed(f[6], 0, q) && q >= 0 && q <= 9 ? static_cast<std::uint8_t>(q) : 0;
    x.satellites = nmea::parse_fixed(f[7], 0, sats) && sats >= 0 && sats < 256 ? static_cast<std::uint8_t>(sats) : 0;
    x.hdop_x100  = nmea::parse_fixed(f[8], 2, hdop) && hdop >= 0 && hdop < 0xFFFF ? static_cast<std::uint16_t>(hdop)
                                                                                    : GnssFix::kUnknown16;
    if (x.quality != 0 && nmea::parse_coord(f[2], f[3], true, x.lat_e7) && nmea::parse_coord(f[4], f[5], false, x.lon_e7)) {
        x.flags |= GnssFix::kPositionValid;
    }
    if (x.quality != 0 && nmea::parse_fixed(f[9], 3, alt)) {
        x.alt_mm = alt;
        x.flags |= GnssFix::kAltitudeValid;
    }
    fix_ = x;
    return true;
}

// RMC: 1 time, 2 status A/V, 3 lat, 4 N/S, 5 lon, 6 E/W, 7 speed kn, 8 course, 9 date ddmmyy, ...
bool NmeaParser::parse_rmc(char** f, std::size_t n) {
    if (n < 10) return false;
    GnssFix x = fix_;
    x.flags = static_cast<std::uint8_t>(x.flags & (GnssFix::kAltitudeValid));
    if (nmea::parse_time(f[1], x.time_ms)) x.flags |= GnssFix::kTimeValid;
    if (nmea::parse_date(f[9], x.year, x.month, x.day)) x.flags |= GnssFix::kDateValid;
    const bool active = f[2][0] == 'A';
    if (active && nmea::parse_coord(f[3], f[4], true, x.lat_e7) && nmea::parse_coord(f[5], f[6], false, x.lon_e7)) {
        x.flags |= GnssFix::kPositionValid;
    } else {
        x.flags = static_cast<std::uint8_t>(x.flags & ~GnssFix::kAltitudeValid);
    }
    if (!knots_to_cms(f[7], x.speed_cms)) x.speed_cms = GnssFix::kUnknown16;
    std::int32_t cog = 0;
    x.cog_cdeg = nmea::parse_fixed(f[8], 2, cog) && cog >= 0 && cog < 36000 ? static_cast<std::uint16_t>(cog)
                                                                             : GnssFix::kUnknown16;
    fix_ = x;
    return true;
}

}  // namespace device
