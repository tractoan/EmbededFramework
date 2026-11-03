#include "utils/fixed_point.hpp"

#include <cstdio>

namespace utils {

std::size_t format_fixed(char* buf, std::size_t size, std::int32_t value, std::uint8_t decimals) {
    if (buf == nullptr || size == 0) return 0;
    if (decimals > 9) decimals = 9;

    std::uint32_t scale = 1;
    for (std::uint8_t i = 0; i < decimals; ++i) scale *= 10;

    // Dùng 64-bit để INT32_MIN không tràn khi đổi dấu.
    const std::int64_t v      = value;
    const std::uint64_t mag   = static_cast<std::uint64_t>(v < 0 ? -v : v);
    const unsigned long whole = static_cast<unsigned long>(mag / scale);
    const unsigned long frac  = static_cast<unsigned long>(mag % scale);
    const char* sign          = value < 0 ? "-" : "";

    const int n = decimals == 0 ? std::snprintf(buf, size, "%s%lu", sign, whole)
                                : std::snprintf(buf, size, "%s%lu.%0*lu", sign, whole, static_cast<int>(decimals), frac);
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n) < size ? static_cast<std::size_t>(n) : size - 1;
}

}  // namespace utils
