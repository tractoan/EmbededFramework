#pragma once
#include "core/noncopyable.hpp"
#include "osal/types.hpp"
#include "osal_port_types.hpp"   // do port cung cấp: osal::port::MutexHandle

namespace osal {

// Mutex không đệ quy. Không dùng trong ISR.
class Mutex : core::NonCopyable {
public:
    Mutex();
    ~Mutex();

    void lock();
    bool try_lock(std::uint32_t timeout_ms = kNoWait);
    void unlock();

private:
    port::MutexHandle h_;
};

class LockGuard : core::NonCopyable {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }

private:
    Mutex& m_;
};

}  // namespace osal
