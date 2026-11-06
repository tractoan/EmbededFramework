#pragma once
#include "core/noncopyable.hpp"
#include "osal/types.hpp"
#include "osal_port_types.hpp"

namespace osal {

// Counting semaphore. give_from_isr() là API duy nhất an toàn trong ngắt.
class Semaphore : core::NonCopyable {
public:
    Semaphore(std::uint32_t initial, std::uint32_t max);
    ~Semaphore();

    bool take(std::uint32_t timeout_ms = kWaitForever);
    void give();
    void give_from_isr();

private:
    port::SemaphoreHandle h_;
};

}  // namespace osal
