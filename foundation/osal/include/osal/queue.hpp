#pragma once
#include <array>
#include <cstddef>

#include "osal/mutex.hpp"
#include "osal/semaphore.hpp"

namespace osal {

// Hàng đợi bounded, cấp phát tĩnh, thread-safe. Viết trên Mutex + Semaphore nên
// tự động portable sang mọi port OSAL (không cần hiện thực riêng cho từng OS).
template <typename T, std::size_t N>
class Queue : core::NonCopyable {
    static_assert(N > 0, "Queue depth must be > 0");

public:
    Queue() : items_(0, static_cast<std::uint32_t>(N)), slots_(static_cast<std::uint32_t>(N), static_cast<std::uint32_t>(N)) {}

    bool send(const T& item, std::uint32_t timeout_ms = kWaitForever) {
        if (!slots_.take(timeout_ms)) return false;
        {
            LockGuard g(mtx_);
            buf_[tail_] = item;
            tail_ = (tail_ + 1) % N;
            ++count_;
        }
        items_.give();
        return true;
    }

    bool receive(T& out, std::uint32_t timeout_ms = kWaitForever) {
        if (!items_.take(timeout_ms)) return false;
        {
            LockGuard g(mtx_);
            out = buf_[head_];
            head_ = (head_ + 1) % N;
            --count_;
        }
        slots_.give();
        return true;
    }

    std::size_t size() const {
        LockGuard g(mtx_);
        return count_;
    }
    static constexpr std::size_t capacity() { return N; }

private:
    std::array<T, N> buf_{};
    std::size_t head_  = 0;
    std::size_t tail_  = 0;
    std::size_t count_ = 0;
    mutable Mutex mtx_;
    Semaphore items_;
    Semaphore slots_;
};

}  // namespace osal
