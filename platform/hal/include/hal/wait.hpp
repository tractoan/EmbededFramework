#pragma once
// Chờ trong driver HAL mà KHÔNG phụ thuộc OSAL (đảo phụ thuộc).
//
// Một số driver phải chờ (UART polling chờ dữ liệu, EEPROM chờ chu kỳ ghi...). HAL không được gọi
// OSAL, nên target – vốn biết RTOS – cài hàm chờ, thường là osal::sleep_ms, khi scheduler đã chạy:
//     hal::set_wait_function(&osal::sleep_ms);
// Chưa cài (bare-metal, trước khi scheduler chạy): wait_ms() trả false và driver tự quay chờ.
#include <cstdint>

namespace hal {

using WaitFunction = void (*)(std::uint32_t ms);

void set_wait_function(WaitFunction fn);
bool wait_ms(std::uint32_t ms);   // true nếu đã chờ bằng hàm của target (nhường CPU)

}  // namespace hal
