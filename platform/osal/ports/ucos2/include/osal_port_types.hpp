#pragma once
// Kiểu native cho port Micrium uC/OS-II (V2.9x).
extern "C" {   // header Micrium là C
#include "ucos_ii.h"
}

namespace osal::port {

struct MutexHandle {
    OS_EVENT* event;   // OSMutexCreate (không ceiling) hoặc semaphore nhị phân – xem osal_ucos2.cpp
};

struct SemaphoreHandle {
    OS_EVENT* event;
    INT16U max;
};

struct ThreadHandle {
    INT8U prio;        // uC/OS-II: mỗi task một priority duy nhất – port tự cấp trong dải của mức logic
    OS_EVENT* done;    // báo hiệu kết thúc để hỗ trợ join()
};

struct CriticalState {
    OS_CPU_SR sr;      // mặt nạ ngắt đã lưu (OS_CRITICAL_METHOD == 3)
};

}  // namespace osal::port
