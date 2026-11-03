#pragma once
#include <cstddef>
#include <cstdint>

namespace osal {

inline constexpr std::uint32_t kNoWait      = 0;
inline constexpr std::uint32_t kWaitForever = 0xFFFFFFFFu;

// Mức ưu tiên logic – mỗi port ánh xạ sang mức ưu tiên native.
enum class Priority : std::uint8_t { Idle = 0, Low, Normal, High, Realtime };

}  // namespace osal
