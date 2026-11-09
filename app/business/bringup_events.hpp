#pragma once
// Mô hình dữ liệu chuẩn của nhóm module bring-up phần cứng (serial/GNSS, I/O số, IMU).
// Giao thức (MAVLink, CLI...) chỉ ánh xạ các Event này, không biết tới module.
//
// Kích thước: payload inline giữ <= 16 byte (mặc định framework); dữ liệu lớn hoặc dài thay đổi
// nằm trong Event::buffer (msg::Buffer đếm tham chiếu, cấp từ pool TĨNH của sản phẩm):
//   - "record": cả struct nằm trong buffer          -> make_record() / get_record()
//   - header inline + mảng byte/phần tử trong buffer -> Event::make(id, header, buffer)
// Hết pool -> event mang buffer rỗng, phía nhận bỏ qua (đếm ở "sys buffers").
#include <cstdint>
#include <cstring>

#include "device/nmea.hpp"
#include "messaging/event.hpp"

namespace app::bringup {

using msg::EventId;
inline constexpr EventId kBase             = msg::evt::kAppBase + 0x100;
inline constexpr EventId kBringupStatus    = kBase + 1;   // record BringupStatus          (1 s)
inline constexpr EventId kLoopbackStatus   = kBase + 2;   // LoopbackHeader + PortResult[count]  (ser.report_ms)
inline constexpr EventId kIoStatus         = kBase + 3;   // inline IoStatus               (khi đổi + định kỳ)
inline constexpr EventId kOutputSetRequest = kBase + 4;   // inline OutputSetRequest       (yêu cầu từ giao diện / CLI)
inline constexpr EventId kImuSample        = kBase + 5;   // record ImuSample              (imu.rate_hz)
inline constexpr EventId kGnssRxData       = kBase + 6;   // GnssDataHeader + byte         (dữ liệu từ máy thu)
inline constexpr EventId kGnssTxRequest    = kBase + 7;   // GnssTxHeader + byte           (lệnh cấu hình tới máy thu)
inline constexpr EventId kStatusText       = kBase + 8;   // StatusHeader + văn bản (không có '\0')
inline constexpr EventId kGnssFix          = kBase + 9;   // record device::GnssFix (giải mã NMEA: giờ, ngày, lat, lon...)

inline constexpr std::size_t kMaxSerial     = 16;    // ok_mask/tested_mask 16 bit
inline constexpr std::size_t kGnssChunkMax  = 512;   // byte GNSS tối đa trong một event
inline constexpr std::size_t kStatusTextMax = 50;    // = STATUSTEXT.text của MAVLink

// Giá trị số trùng với enum trong tools/mavlink/bringup.xml
enum class GnssState : std::uint8_t { Detecting = 0, Connected = 1, Absent = 2 };
enum class SerialRole : std::uint8_t { Loopback = 0, Gcs = 1, Gnss = 2, Disabled = 3 };
enum class Severity : std::uint8_t { Emergency = 0, Alert, Critical, Error, Warning, Notice, Info, Debug };   // RFC 5424

inline const char* to_string(GnssState s) {
    switch (s) {
        case GnssState::Detecting: return "DETECTING";
        case GnssState::Connected: return "CONNECTED";
        case GnssState::Absent:    return "ABSENT(loopback)";
    }
    return "?";
}

struct BringupStatus {
    std::uint32_t uptime_ms;
    std::uint32_t gnss_rx_bytes;
    std::uint32_t gnss_tx_bytes;
    std::uint32_t gnss_baudrate;
    GnssState gnss_state;
    std::uint8_t gcs_port;      // 0xFF = không có
    std::uint8_t gnss_port;
    std::uint8_t serial_count;
};

struct LoopbackHeader {
    std::uint16_t ok_mask;       // bit i: lần thử gần nhất của cổng i PASS
    std::uint16_t tested_mask;   // bit i: cổng i đang được thử loopback
    std::uint8_t count;          // số PortResult trong buffer
};
struct PortResult {
    std::uint32_t pass;
    std::uint32_t fail;
    std::uint8_t role;     // SerialRole
    std::uint8_t result;   // svc::LoopResult
};

struct IoStatus {
    std::uint32_t time_ms;
    std::uint32_t inputs;    // bit i = DI i
    std::uint32_t outputs;   // bit i = DO i (giá trị đã ghi)
    std::uint8_t input_count;
    std::uint8_t output_count;
};

struct OutputSetRequest {
    std::uint32_t mask;    // chỉ các bit này được ghi
    std::uint32_t value;
};

enum ImuFlags : std::uint8_t { kImuFresh = 1, kImuEverReceived = 2, kImuStartFailed = 4 };

struct ImuSample {
    std::uint32_t time_ms;
    std::uint32_t frames;
    std::uint32_t errors;   // FCS + định dạng
    float gyro[3];          // rad/s
    float accel[3];         // m/s²
    float temperature;      // NaN nếu không có
    std::uint16_t status;
    std::uint16_t rate_hz;
    std::uint8_t imu;       // chỉ số IMU
    std::uint8_t type;      // ImuFormat::type_id
    std::uint8_t flags;     // ImuFlags
    std::uint8_t raw_len;
    std::uint8_t raw[48];
};

struct GnssDataHeader {
    std::uint16_t len;   // = buffer.size()
};

struct GnssTxHeader {
    std::uint32_t baudrate;   // != 0: đổi tốc độ cổng máy thu trước khi gửi (buffer có thể rỗng)
};

struct StatusHeader {
    Severity severity;
};

static_assert(sizeof(IoStatus) <= core::cfg::kEventPayloadSize && sizeof(OutputSetRequest) <= core::cfg::kEventPayloadSize &&
                  sizeof(LoopbackHeader) <= core::cfg::kEventPayloadSize,
              "bring-up inline payloads must fit the default event payload");

// Cả struct nằm trong Event::buffer.
template <typename T>
msg::Event make_record(EventId id, const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "record must be trivially copyable");
    return msg::Event::make(id, msg::Buffer::copy_of(&value, sizeof(T)));
}

template <typename T>
bool get_record(const msg::Event& e, T& out) {
    if (e.buffer.size() != sizeof(T)) return false;
    std::memcpy(&out, e.buffer.data(), sizeof(T));
    return true;
}

}  // namespace app::bringup
