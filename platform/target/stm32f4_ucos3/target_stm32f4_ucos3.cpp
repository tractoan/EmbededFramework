// Target STM32F4 + uC/OS-III (+ uC/CPU).
// Ứng dụng chạy trong task "init" sau OSStart(). Mọi object OSAL được tạo sau OSInit().
extern "C" {
#include "os.h"
#include "stm32f4xx_hal.h"
void SystemClock_Config(void);       // platform/target/common/stm32f4/clock_config.c
void MX_GPIO_Init(void);             // CubeMX: gpio.c
void MX_USART2_UART_Init(void);      // CubeMX: usart.c
void MX_USART3_UART_Init(void);      // CubeMX: usart.c (Aux / RS485)
void MX_I2C1_Init(void);             // CubeMX: i2c.c
}

#include "hal/board.hpp"
#include "osal/thread.hpp"
#include "osal/time.hpp"
#include "target/target.hpp"

namespace target {

namespace {
alignas(8) CPU_STK g_init_stack[1024];
osal::Thread g_init_thread;
AppMain g_app_main = nullptr;

[[noreturn]] void halt() {
    for (;;) {}
}

void init_task(void*) {
    // uC/OS: bộ đếm tick phải được khởi động từ task đầu tiên (sau OSStart).
    OS_CPU_SysTickInit(HAL_RCC_GetHCLKFreq() / OSCfg_TickRate_Hz);
#if OS_CFG_STAT_TASK_EN > 0u
    OS_ERR err = OS_ERR_NONE;
    OSStatTaskCPUUsageInit(&err);
#endif
    if (g_app_main() != 0) hal::board().reset();
    for (;;) osal::sleep_ms(1000);   // có thể dùng làm watchdog/idle supervisor
}
}  // namespace

void init(int /*argc*/, char** /*argv*/) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();
    MX_I2C1_Init();
    CPU_Init();
    OS_ERR err = OS_ERR_NONE;
    OSInit(&err);   // phải trước khi tạo bất kỳ object OSAL nào
    if (err != OS_ERR_NONE) halt();
}

int run(AppMain app_main) {
    g_app_main = app_main;
    if (!core::is_ok(g_init_thread.start(
            osal::ThreadConfig{"init", osal::Priority::High, sizeof(g_init_stack), g_init_stack}, &init_task, nullptr))) {
        halt();
    }
    OS_ERR err = OS_ERR_NONE;
    OSStart(&err);
    halt();   // chỉ tới đây nếu OSStart thất bại
}

bool stop_requested() { return false; }

}  // namespace target

// ---- Ngắt hệ thống chuyển cho uC/OS (CubeMX không được sinh 2 handler này) ----
extern "C" void SysTick_Handler(void) { OS_CPU_SysTickHandler(); }

// PendSV thực hiện chuyển ngữ cảnh: phải nhảy thẳng (không tạo stack frame) vào handler của port.
extern "C" __attribute__((naked)) void PendSV_Handler(void) { __asm volatile("b OS_CPU_PendSVHandler"); }
