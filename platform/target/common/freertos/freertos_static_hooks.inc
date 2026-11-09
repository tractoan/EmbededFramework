// Hook bắt buộc của FreeRTOS khi chỉ dùng cấp phát tĩnh (configSUPPORT_STATIC_ALLOCATION = 1)
// và kiểm tra tràn stack. Dùng chung cho mọi target FreeRTOS: #include file này trong CÙNG file
// .cpp với target::init() để chắc chắn được link vào firmware (thư viện tĩnh chỉ kéo object nào
// được tham chiếu). Hook của BSP (vd. Xilinx) thường là weak nên bản này ghi đè được.
//
// Yêu cầu trước khi include: đã include "FreeRTOS.h" và "task.h".
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
    for (;;) {}   // watchdog (nếu bật) sẽ reset; gắn debugger để xem task nào tràn
}

extern "C" void vApplicationMallocFailedHook(void) {
    taskDISABLE_INTERRUPTS();
    for (;;) {}   // không được xảy ra: framework không dùng heap FreeRTOS
}
