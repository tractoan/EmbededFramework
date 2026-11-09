// Target Zynq UltraScale+ MPSoC – RPU (Cortex-R5F) + FreeRTOS (BSP standalone của Xilinx/Vitis).
// Clock, MPU, cache, GIC và tick timer (TTC) do BSP + port FreeRTOS của Xilinx khởi tạo
// (boot.S / vPortSetupTimerInterrupt) -> init() chỉ cài chính sách bộ nhớ.
// Ứng dụng chạy trong task "init" sau khi scheduler đã chạy.
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

#include "hal/board.hpp"
#include "hal/wait.hpp"
#include "osal/memory.hpp"
#include "osal/thread.hpp"
#include "osal/time.hpp"
#include "target/target.hpp"

namespace target {

namespace {
void on_heap_violation(std::size_t /*bytes*/) { hal::board().reset(); }

// HAL chờ (vd. I2C/EEPROM) bằng hàm này: nhường CPU thay vì busy-wait khi kernel đã chạy.
void rtos_wait_ms(std::uint32_t ms) { osal::sleep_ms(ms); }

alignas(8) StackType_t g_init_stack[1024];
osal::Thread g_init_thread;
AppMain g_app_main = nullptr;

void init_task(void*) {
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
    vTaskStartScheduler();
    for (;;) {}
}

bool stop_requested() { return false; }

}  // namespace target

#include "../common/freertos/freertos_static_hooks.inc"
