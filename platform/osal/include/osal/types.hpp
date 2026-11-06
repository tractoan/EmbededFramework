#pragma once
#include <cstddef>
#include <cstdint>

namespace osal {

inline constexpr std::uint32_t kNoWait      = 0;
inline constexpr std::uint32_t kWaitForever = 0xFFFFFFFFu;

// Mức ưu tiên logic: 0 (thấp nhất) .. 15 (cao nhất), giống nhau trên mọi RTOS.
// Mỗi port ánh xạ tuyến tính sang priority native (FreeRTOS: số lớn = cao; uC/OS: số nhỏ = cao).
//   osal::Priority::Normal              mức có tên
//   osal::Priority{11}                  mức tùy ý
//   osal::Priority::High.lower(1)       tương đối
class Priority {
public:
    static constexpr std::uint8_t kLevels = 16;
    static constexpr std::uint8_t kMax    = kLevels - 1;

    constexpr Priority() : level_(8) {}   // = Normal
    constexpr explicit Priority(std::uint8_t level) : level_(level > kMax ? kMax : level) {}
    constexpr std::uint8_t level() const { return level_; }

    constexpr Priority raise(std::uint8_t n) const {
        return Priority(static_cast<std::uint8_t>(level_ + n > kMax ? kMax : level_ + n));
    }
    constexpr Priority lower(std::uint8_t n) const {
        return Priority(static_cast<std::uint8_t>(n > level_ ? 0 : level_ - n));
    }
    constexpr bool operator==(Priority o) const { return level_ == o.level_; }
    constexpr bool operator!=(Priority o) const { return level_ != o.level_; }
    constexpr bool operator<(Priority o) const { return level_ < o.level_; }

    static const Priority Idle;
    static const Priority Low;
    static const Priority Normal;
    static const Priority High;
    static const Priority Realtime;

private:
    std::uint8_t level_;
};

inline constexpr Priority Priority::Idle{0};
inline constexpr Priority Priority::Low{4};
inline constexpr Priority Priority::Normal{8};
inline constexpr Priority Priority::High{12};
inline constexpr Priority Priority::Realtime{15};

}  // namespace osal
