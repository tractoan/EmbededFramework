// Target Intel Nios II + uC/OS-II (BSP loại "UCOSII" của Nios II SBT).
// Khác target uC/OS-II của STM32: BSP đã làm sẵn trước main():
//   - alt_main() gọi ALT_OS_INIT() = OSInit()  -> KHÔNG gọi OSInit() lại (sẽ xóa semaphore HAL đã tạo);
//   - timer hệ thống (hal.sys_clk_timer) gọi OSTimeTick() qua alt_tick() -> không cần cài tick.
// Ứng dụng chạy trong task "init" sau OSStart().
extern "C" {
#include "system.h"
#include "ucos_ii.h"
}

#include "hal/board.hpp"
#include "hal/wait.hpp"
#include "osal/memory.hpp"
#include "osal/thread.hpp"
#include "osal/time.hpp"
#include "target/target.hpp"

namespace target {

namespace {
// Cấp phát heap sau khởi động: vi phạm chính sách bộ nhớ -> về trạng thái đã biết.
void on_heap_violation(std::size_t /*bytes*/) { hal::board().reset(); }

// HAL chờ (UART, I2C) bằng hàm này khi kernel đã chạy: nhường CPU thay vì chờ bận.
void rtos_wait_ms(std::uint32_t ms) { osal::sleep_ms(ms); }

alignas(8) OS_STK g_init_stack[1024];
osal::Thread g_init_thread;
AppMain g_app_main = nullptr;

void init_task(void*) {
#if OS_TASK_STAT_EN > 0u
    OSStatInit();
#endif
    hal::set_wait_function(&rtos_wait_ms);
    if (g_app_main() != 0) hal::board().reset();
    for (;;) osal::sleep_ms(1000);
}
}  // namespace

void init(int /*argc*/, char** /*argv*/) { osal::set_heap_violation_handler(&on_heap_violation); }

int run(AppMain app_main) {
    g_app_main = app_main;
    g_init_thread.start(osal::ThreadConfig{"init", osal::Priority::High, sizeof(g_init_stack), g_init_stack},
                        &init_task, nullptr);
    OSStart();
    for (;;) {}
}

bool stop_requested() { return false; }

}  // namespace target
