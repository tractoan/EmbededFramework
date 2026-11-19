#pragma once
// Bộ giải mã NMEA 0183 cho máy thu GNSS: hàm thuần (không thread, không heap, không phụ thuộc OS),
// nhận từng byte, ghép câu "$....*hh\r\n", kiểm tra checksum, giải mã GGA và RMC của mọi talker
// (GP, GN, GL, GA, GB/BD...). Toạ độ/thời gian dùng số nguyên (không cần FPU).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace device {

// Kiểu trivial (đi qua msg::Topic bằng memcpy): không có giá trị mặc định trong khai báo; GnssFix{} = toàn 0,
// GnssFix::empty() = chưa có dữ liệu (hdop/tốc độ/hướng = kUnknown16).
struct GnssFix {
    enum Flags : std::uint8_t { kTimeValid = 1, kDateValid = 2, kPositionValid = 4, kAltitudeValid = 8 };
    static constexpr std::uint16_t kUnknown16 = 0xFFFF;

    std::uint32_t time_ms;         // giờ UTC trong ngày (ms từ 00:00:00)
    std::uint16_t year;            // ngày UTC (RMC), 0 = chưa biết
    std::uint8_t month;
    std::uint8_t day;
    std::int32_t lat_e7;           // độ x 1e7, Bắc dương
    std::int32_t lon_e7;           // độ x 1e7, Đông dương
    std::int32_t alt_mm;           // độ cao so với mực nước biển (GGA)
    std::uint16_t hdop_x100;
    std::uint16_t speed_cms;    // tốc độ mặt đất (RMC)
    std::uint16_t cog_cdeg;     // hướng đi 0.01° (RMC)
    std::uint8_t quality;          // GGA: 0 không fix, 1 GPS, 2 DGPS, 4 RTK fixed, 5 RTK float, 6 ước lượng
    std::uint8_t satellites;       // GGA: số vệ tinh dùng trong lời giải
    std::uint8_t flags;            // Flags

    static constexpr GnssFix empty() {
        GnssFix f{};
        f.hdop_x100 = kUnknown16;
        f.speed_cms = kUnknown16;
        f.cog_cdeg  = kUnknown16;
        return f;
    }

    // Thời gian UNIX (µs) từ ngày + giờ UTC; 0 nếu chưa có ngày.
    std::uint64_t unix_time_us() const;
};
static_assert(std::is_trivial<GnssFix>::value, "GnssFix must stay trivial (msg::Topic copies it with memcpy)");

class NmeaParser {
public:
    enum class Result : std::uint8_t { None, Gga, Rmc, OtherSentence, ChecksumError, Malformed };
    static constexpr std::size_t kMaxSentence = 96;   // chuẩn: <= 82 ký tự; chừa chỗ cho câu riêng của hãng

    // Nạp một byte. Trả kết quả khi vừa kết thúc một câu (gặp CR/LF), None nếu chưa xong.
    Result feed(char c);
    // Tiện ích: nạp cả khối, gọi on_fix(const GnssFix&, Result) sau mỗi câu GGA/RMC hợp lệ.
    template <typename F>
    void feed(const std::uint8_t* data, std::size_t len, F&& on_fix) {
        for (std::size_t i = 0; i < len; ++i) {
            const Result r = feed(static_cast<char>(data[i]));
            if (r == Result::Gga || r == Result::Rmc) on_fix(fix_, r);
        }
    }

    const GnssFix& fix() const { return fix_; }   // trạng thái tổng hợp từ các câu gần nhất
    // Bộ đếm đọc được từ thread khác (CLI, báo cáo) trong khi thread lấy mẫu đang nạp byte.
    std::uint32_t sentences() const { return sentences_.load(std::memory_order_relaxed); }   // câu đúng checksum
    std::uint32_t checksum_errors() const { return checksum_errors_.load(std::memory_order_relaxed); }
    void reset();

private:
    Result finish();
    bool parse_gga(char** f, std::size_t n);
    bool parse_rmc(char** f, std::size_t n);

    char line_[kMaxSentence + 1]{};
    std::size_t len_    = 0;
    bool in_sentence_   = false;
    GnssFix fix_ = GnssFix::empty();
    std::atomic<std::uint32_t> sentences_{0}, checksum_errors_{0};
};

// Gom các câu NMEA thành MỘT fix mỗi chu kỳ đo (epoch) – dạng Parser của svc::StreamSensorTask:
//   bool feed(std::uint8_t byte, GnssFix& out)   // true: `out` vừa nhận một fix hoàn chỉnh
// Luật (không cần đồng hồ, đúng với mọi tần số đo):
//   - GGA kết thúc một epoch -> ra fix (kèm ngày/tốc độ/hướng từ RMC gần nhất);
//   - RMC chỉ ra fix khi máy thu không phát GGA: chưa từng thấy GGA, hoặc đã gặp >= 2 RMC kể từ GGA cuối
//     (GGA bị tắt giữa chừng: chậm một epoch rồi gom theo RMC).
//
//   static device::NmeaFixParser nmea;
//   static msg::Topic<device::GnssFix> gnss_topic;
//   static svc::StreamSensorTask<device::NmeaFixParser, device::GnssFix> gnss{"gnss", uart, nmea, gnss_topic, 1500};
class NmeaFixParser {
public:
    bool feed(std::uint8_t byte, GnssFix& out);
    NmeaParser::Result last_result() const { return last_; }   // kết quả câu vừa kết thúc (chẩn đoán)
    const NmeaParser& nmea() const { return p_; }             // số câu, lỗi checksum, fix tổng hợp
    std::uint32_t fixes() const { return fixes_.load(std::memory_order_relaxed); }   // đọc được từ thread khác
    void reset();

private:
    NmeaParser p_;
    NmeaParser::Result last_ = NmeaParser::Result::None;
    bool seen_gga_            = false;
    std::uint8_t rmc_since_gga_ = 0;
    std::atomic<std::uint32_t> fixes_{0};
};

// Các hàm giải mã trường (public để test). Trả false nếu trường rỗng/sai.
namespace nmea {
bool parse_time(const char* s, std::uint32_t& ms_of_day);                      // hhmmss[.sss]
bool parse_date(const char* s, std::uint16_t& y, std::uint8_t& m, std::uint8_t& d);   // ddmmyy
bool parse_coord(const char* s, const char* hemi, bool is_lat, std::int32_t& e7);  // (d)ddmm.mmmm + N/S/E/W
bool parse_fixed(const char* s, int decimals, std::int32_t& out);              // "12.34", 2 -> 1234
}  // namespace nmea

}  // namespace device
