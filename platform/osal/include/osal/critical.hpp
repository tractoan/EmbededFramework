#pragma once
#include "core/noncopyable.hpp"
#include "osal_port_types.hpp"   // osal::port::CriticalState

namespace osal {

// Vùng găng ngắn (RAII): trên RTOS = tắt ngắt/scheduler, trên POSIX = global mutex.
// Lồng nhau được. CriticalState lưu trạng thái cần khôi phục (vd. mặt nạ ngắt trên uC/OS).
class CriticalSection : core::NonCopyable {
public:
    CriticalSection();
    ~CriticalSection();

private:
    port::CriticalState state_{};
};

}  // namespace osal
