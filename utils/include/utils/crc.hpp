#pragma once
// Checksum dùng cho lưu trữ NV, giao thức truyền thông, kiểm tra firmware.
#include <cstddef>
#include <cstdint>

namespace utils {

// CRC-32 (IEEE 802.3, poly 0xEDB88320 reflected) – dùng cho dữ liệu lưu Flash.
// Nối tiếp nhiều khối: crc = crc32(b, n2, crc32(a, n1)).
std::uint32_t crc32(const void* data, std::size_t len, std::uint32_t crc = 0);

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) – phổ biến trong giao thức UART/BLE.
// Nối tiếp: crc = crc16_ccitt(b, n2, crc16_ccitt(a, n1)).
std::uint16_t crc16_ccitt(const void* data, std::size_t len, std::uint16_t crc = 0xFFFF);

// CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF) – truyền byte thấp trước.
std::uint16_t crc16_modbus(const void* data, std::size_t len, std::uint16_t crc = 0xFFFF);

// CRC-16/MCRF4XX (poly 0x8408 reflected, init 0xFFFF, không xor cuối) – checksum của MAVLink
// ("X.25 accumulate"). Nối tiếp: crc = crc16_mcrf4xx(b, n2, crc16_mcrf4xx(a, n1)).
std::uint16_t crc16_mcrf4xx(const void* data, std::size_t len, std::uint16_t crc = 0xFFFF);

// CRC-16/X-25 (= FCS-16 của HDLC/SDLC, ISO 13239): như MCRF4XX nhưng đảo bit kết quả.
// Truyền byte thấp trước. Không nối tiếp được (đã xor cuối) – tính một lần cho cả khung.
inline std::uint16_t crc16_x25(const void* data, std::size_t len) {
    return static_cast<std::uint16_t>(~crc16_mcrf4xx(data, len));
}

}  // namespace utils
