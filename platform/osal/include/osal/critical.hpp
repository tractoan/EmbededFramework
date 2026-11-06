#pragma once
#include "core/noncopyable.hpp"

namespace osal {

// Vùng găng ngắn (RAII): trên RTOS = tắt ngắt/scheduler, trên POSIX = global mutex.
class CriticalSection : core::NonCopyable {
public:
    CriticalSection();
    ~CriticalSection();
};

}  // namespace osal
