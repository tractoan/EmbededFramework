#pragma once
// Kiểu native cho port FreeRTOS – mọi object kernel cấp phát tĩnh bên trong wrapper.
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

namespace osal::port {

struct MutexHandle {
    StaticSemaphore_t storage;
    SemaphoreHandle_t handle;
};

struct SemaphoreHandle {
    StaticSemaphore_t storage;
    SemaphoreHandle_t handle;
};

struct ThreadHandle {
    StaticTask_t tcb;
    TaskHandle_t handle;
    StaticSemaphore_t done_storage;   // báo hiệu kết thúc để hỗ trợ join()
    SemaphoreHandle_t done;
};

struct CriticalState {};   // taskENTER_CRITICAL tự đếm lồng nhau

}  // namespace osal::port
