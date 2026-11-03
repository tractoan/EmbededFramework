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

}  // namespace utils
