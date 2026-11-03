#pragma once
#include <cstddef>
#include <cstdint>

namespace core {

// CRC-32 (IEEE 802.3), bitwise – nhỏ gọn, không cần bảng tra.
// Có thể nối tiếp: crc = crc32(b, n2, crc32(a, n1)).
inline std::uint32_t crc32(const void* data, std::size_t len, std::uint32_t crc = 0) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

}  // namespace core
