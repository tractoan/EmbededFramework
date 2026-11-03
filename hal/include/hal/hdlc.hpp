#pragma once
#include <cstddef>
#include <cstdint>

#include "core/status.hpp"

namespace hal {

// Cổng nối tiếp đồng bộ dạng khung SDLC/HDLC (vd. kênh SCC/ESCC Z85230, bộ điều khiển HDLC
// trong FPGA, card SyncLink, netdev "hdlcX" của Linux...).
// Bộ điều khiển phần cứng lo cờ 0x7E, bit-stuffing, mã hóa đường truyền và (thường) FCS;
// lớp trên chỉ làm việc với từng KHUNG hoàn chỉnh.
struct HdlcConfig {
    enum class Encoding : std::uint8_t { Nrz, Nrzi, Fm0, Fm1, Manchester };
    enum class Crc : std::uint8_t { None, Crc16Ccitt, Crc32 };
    enum class Clock : std::uint8_t { External, Internal };   // External: IMU cấp xung clock

    std::uint32_t bitrate = 1000000;
    Encoding encoding     = Encoding::Nrz;
    Crc crc               = Crc::Crc16Ccitt;
    Clock clock           = Clock::External;
    std::uint8_t address  = 0;   // 0 = nhận mọi địa chỉ (không lọc)
};

// Trạng thái của khung vừa nhận do phần cứng báo (nếu có).
struct HdlcFrameStatus {
    bool crc_error = false;   // FCS sai (phần cứng kiểm tra)
    bool overrun   = false;   // tràn FIFO/khung dài hơn bộ đệm
    bool aborted   = false;   // nhận chuỗi abort (7 bit 1 liên tiếp)
};

class IHdlcPort {
public:
    virtual ~IHdlcPort() = default;
    virtual core::Status open(const HdlcConfig& cfg) = 0;
    virtual void close()                             = 0;
    // Đọc MỘT khung (không gồm cờ). Trả số byte của khung, 0 khi hết timeout.
    // Khung dài hơn cap bị cắt và đánh dấu overrun.
    virtual std::size_t read_frame(std::uint8_t* buf, std::size_t cap, std::uint32_t timeout_ms,
                                   HdlcFrameStatus* status) = 0;
    virtual core::Status write_frame(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) = 0;
};

}  // namespace hal
