#include "utils/crc.hpp"

namespace utils {

// Bản bitwise: nhỏ gọn (không tốn 1 KB bảng tra). Nếu cần tốc độ, thay bằng bảng
// hoặc bộ CRC phần cứng của MCU mà không ảnh hưởng nơi gọi.
std::uint32_t crc32(const void* data, std::size_t len, std::uint32_t crc) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::uint16_t crc16_ccitt(const void* data, std::size_t len, std::uint16_t crc) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    while (len--) {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(*p++) << 8));
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 0x8000u) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021u) : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::uint16_t crc16_modbus(const void* data, std::size_t len, std::uint16_t crc) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    while (len--) {
        crc = static_cast<std::uint16_t>(crc ^ *p++);
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001u) : static_cast<std::uint16_t>(crc >> 1);
        }
    }
    return crc;
}

std::uint16_t crc16_mcrf4xx(const void* data, std::size_t len, std::uint16_t crc) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    while (len--) {
        // Thuật toán byte-wise của MAVLink (crc_accumulate), tương đương bitwise poly 0x8408.
        std::uint8_t t = static_cast<std::uint8_t>(*p++ ^ (crc & 0xFF));
        t = static_cast<std::uint8_t>(t ^ (t << 4));
        crc = static_cast<std::uint16_t>((crc >> 8) ^ (t << 8) ^ (t << 3) ^ (t >> 4));
    }
    return crc;
}

}  // namespace utils
