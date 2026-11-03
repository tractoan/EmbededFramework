#pragma once
// Số fixed-point dạng số nguyên (vd. 0.1 °C, 0.01 V): tránh float trên MCU không có FPU.
#include <cstddef>
#include <cstdint>

namespace utils {

// Ghi value / 10^decimals ra buf, vd. format_fixed(buf, n, -123, 1) -> "-12.3".
// Trả về số ký tự đã ghi (không tính '\0'); luôn kết thúc chuỗi nếu size > 0.
std::size_t format_fixed(char* buf, std::size_t size, std::int32_t value, std::uint8_t decimals);

// Tiện cho printf/log: LOGI(tag, "T=%s C", utils::Fixed(t, 1).c_str());
class Fixed {
public:
    Fixed(std::int32_t value, std::uint8_t decimals) { format_fixed(buf_, sizeof(buf_), value, decimals); }
    const char* c_str() const { return buf_; }

private:
    char buf_[16];
};

}  // namespace utils
