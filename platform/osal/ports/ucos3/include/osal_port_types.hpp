#pragma once
// Kiểu native cho port Micrium uC/OS-III (V3.0x). Object kernel nằm ngay trong wrapper (cấp phát tĩnh).
extern "C" {   // header Micrium là C
#include "os.h"
}

namespace osal::port {

struct MutexHandle {
    OS_MUTEX mutex;
};

struct SemaphoreHandle {
    OS_SEM sem;
    OS_SEM_CTR max;
};

struct ThreadHandle {
    OS_TCB tcb;   // TCB thuộc về Thread -> join() phải xóa task trước khi Thread bị hủy
    OS_SEM done;  // báo hiệu kết thúc để hỗ trợ join()
};

struct CriticalState {
    CPU_SR sr;    // mặt nạ ngắt đã lưu (uC/CPU)
};

}  // namespace osal::port
