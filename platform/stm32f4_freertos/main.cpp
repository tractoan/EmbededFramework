// Entry point cho STM32F4 + FreeRTOS.
// Toàn bộ khởi tạo ứng dụng chạy trong task "init" sau khi scheduler đã chạy
// (vì services tạo thread/mutex và cần kernel hoạt động).
#include "FreeRTOS.h"
#include "task.h"

#include "app/product.hpp"
#include "app/system.hpp"
#include "osal/thread.hpp"
#include "osal/time.hpp"

extern "C" {
#include "stm32f4xx_hal.h"
void SystemClock_Config(void);       // platform/stm32f4_freertos/clock_config.c
void MX_GPIO_Init(void);             // CubeMX: gpio.c
void MX_USART2_UART_Init(void);      // CubeMX: usart.c
void MX_USART3_UART_Init(void);      // CubeMX: usart.c (Aux / RS485)
void MX_I2C1_Init(void);             // CubeMX: i2c.c
}

namespace {

alignas(8) StackType_t g_init_stack[1024];
osal::Thread g_init_thread;

void init_task(void*) {
    static app::System system(hal::board());
    if (!core::is_ok(system.init(product::name(), &product::setup))) {
        hal::board().reset();
    }
    for (;;) osal::sleep_ms(1000);   // có thể dùng làm watchdog/idle supervisor
}

}  // namespace

int main() {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();
    MX_I2C1_Init();

    g_init_thread.start(osal::ThreadConfig{"init", osal::Priority::High, sizeof(g_init_stack), g_init_stack},
                        &init_task, nullptr);
    vTaskStartScheduler();
    for (;;) {}
}

// ---- Hook bắt buộc của FreeRTOS khi bật cấp phát tĩnh / kiểm tra stack ----
extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** tcb, StackType_t** stack, uint32_t* size) {
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb   = &idle_tcb;
    *stack = idle_stack;
    *size  = configMINIMAL_STACK_SIZE;
}

extern "C" void vApplicationGetTimerTaskMemory(StaticTask_t** tcb, StackType_t** stack, uint32_t* size) {
    static StaticTask_t timer_tcb;
    static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];
    *tcb   = &timer_tcb;
    *stack = timer_stack;
    *size  = configTIMER_TASK_STACK_DEPTH;
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char*) {
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

extern "C" void vApplicationMallocFailedHook(void) {
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}
