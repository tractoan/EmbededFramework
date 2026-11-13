#pragma once
// Ánh xạ phần cứng của bo mạch cần bring-up -> chức năng kiểm tra.
// Chỉ số cổng là chỉ số LOGIC của board (hal::IBoard::serial(i), digital_input(i)...);
// board port (hal/ports/<board>) ánh xạ tiếp sang /dev/ttyXXX, chân GPIO, kênh HDLC.
#include <cstddef>
#include <cstdint>

#include "hal/hdlc.hpp"
#include "device/imu.hpp"

namespace product::hw {

// ---------------------------------------------------------------- Serial
inline constexpr std::size_t kSerialCount = 12;
inline constexpr int kGcsPort  = 0;   // phần mềm giao diện (MAVLink)
inline constexpr int kGnssPort = 1;   // máy thu GNSS (không có dữ liệu trong gnss.detect_ms -> loopback)
inline constexpr std::uint32_t kGcsBaudrate      = 115200;
// Kênh MAVLink thứ hai qua UDP (chỉ khi target có mạng): firmware nghe cổng này, trả lời địa chỉ gửi
// gần nhất (phần mềm giao diện gửi HEARTBEAT trước). 0 = tắt.
inline constexpr std::uint16_t kGcsUdpPort       = 14560;
inline constexpr std::uint32_t kLoopbackBaudrate = 115200;
// Loopback nối chéo từng cặp (vd. RS485 2 dây không tự nghe được chính nó): kLoopbackPeer[a] = b
// và kLoopbackPeer[b] = a. -1 = giắc loopback TX->RX trên chính cổng (RS232, RS422/RS485 4 dây).
inline constexpr std::int8_t kLoopbackPeer[kSerialCount] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

// ---------------------------------------------------------------- I/O số
inline constexpr std::size_t kInputCount  = 20;
inline constexpr std::size_t kOutputCount = 20;
inline constexpr std::uint32_t kInputInvertMask = 0;   // bit = 1: kênh tích cực mức thấp

// ---------------------------------------------------------------- IMU (SDLC/HDLC)
inline constexpr std::size_t kImuCount = 2;
inline const device::ImuFormat* const kImuFormat[kImuCount] = {&device::imu_formats::kLn200, &device::imu_formats::kHg1700};

inline hal::HdlcConfig imu_port_config(std::size_t /*imu*/) {
    hal::HdlcConfig c;
    c.bitrate  = 1000000;                          // SDLC 1 Mbit/s
    c.encoding = hal::HdlcConfig::Encoding::Nrz;
    c.crc      = hal::HdlcConfig::Crc::Crc16Ccitt;  // phần cứng kiểm tra FCS-16
    c.clock    = hal::HdlcConfig::Clock::External;  // IMU cấp clock
    return c;
}
inline constexpr bool kImuSoftwareFcs = false;      // true nếu bộ điều khiển trả cả 2 byte FCS
// Mỗi lần đọc chờ tối đa thời gian này một khung hợp lệ; 3 lần liên tiếp không có -> kSensorFault.
inline constexpr std::uint32_t kImuReadTimeoutMs = 100;

}  // namespace product::hw
