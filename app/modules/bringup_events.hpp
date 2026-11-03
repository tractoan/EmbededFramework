#pragma once
// Mô hình dữ liệu chuẩn của nhóm module bring-up phần cứng (serial/GNSS, I/O số, IMU).
// Giao thức (MAVLink, CLI...) chỉ ánh xạ các Event này, không biết tới module.
// Payload lớn nhất ~128 byte -> sản phẩm dùng nhóm này đặt EP_CFG_EVENT_PAYLOAD_SIZE >= 128.
#include <cstdint>

#include "services/event.hpp"

namespace app::bringup {

using svc::EventId;
inline constexpr EventId kBase             = svc::evt::kAppBase + 0x100;
inline constexpr EventId kBringupStatus    = kBase + 1;   // BringupStatus      (định kỳ 1 s)
inline constexpr EventId kLoopbackStatus   = kBase + 2;   // LoopbackStatus     (định kỳ ser.report_ms)
inline constexpr EventId kIoStatus         = kBase + 3;   // IoStatus           (khi đổi + định kỳ)
inline constexpr EventId kOutputSetRequest = kBase + 4;   // OutputSetRequest   (yêu cầu từ giao diện)
inline constexpr EventId kImuSample        = kBase + 5;   // ImuSample          (imu.rate_hz)
inline constexpr EventId kGnssRxData       = kBase + 6;   // GnssData           (byte từ máy thu)
inline constexpr EventId kGnssTxRequest    = kBase + 7;   // GnssTxRequest      (lệnh cấu hình tới máy thu)
inline constexpr EventId kStatusText       = kBase + 8;   // StatusText         (thông báo cho người vận hành)

inline constexpr std::size_t kMaxSerial = 12;
inline constexpr std::size_t kGnssChunk = 70;   // = SERIAL_CONTROL.data của MAVLink

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

struct LoopbackStatus {
    std::uint16_t ok_mask;       // bit i: lần thử gần nhất của cổng i PASS
    std::uint16_t tested_mask;   // bit i: cổng i đang được thử loopback
    std::uint8_t count;
    std::uint8_t role[kMaxSerial];     // SerialRole
    std::uint8_t result[kMaxSerial];   // svc::LoopResult
    std::uint32_t pass[kMaxSerial];
    std::uint32_t fail[kMaxSerial];
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

struct GnssData {
    std::uint8_t len;
    std::uint8_t data[kGnssChunk];
};

struct GnssTxRequest {
    std::uint32_t baudrate;   // != 0: đổi tốc độ cổng máy thu trước khi gửi
    std::uint8_t len;
    std::uint8_t data[kGnssChunk];
};

struct StatusText {
    Severity severity;
    char text[50];   // = STATUSTEXT.text của MAVLink
};

static_assert(core::cfg::kEventPayloadSize >= sizeof(LoopbackStatus) && core::cfg::kEventPayloadSize >= sizeof(ImuSample),
              "bring-up events need EP_CFG_EVENT_PAYLOAD_SIZE >= 128 (products/<p>/config/product_config.hpp)");

}  // namespace app::bringup
