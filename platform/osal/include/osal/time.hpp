#pragma once
#include <cstdint>

namespace osal {

std::uint64_t now_ms();             // thời gian đơn điệu từ lúc khởi động
void sleep_ms(std::uint32_t ms);
void yield();

}  // namespace osal
