#pragma once
// Ring buffer lock-free 1 producer / 1 consumer (SPSC).
// Trường hợp điển hình: ISR UART/DMA đẩy byte vào (producer), thread đọc ra (consumer)
// mà không cần tắt ngắt hay mutex. N phải là lũy thừa của 2; dung lượng thực = N - 1.
#include <array>
#include <atomic>
#include <cstddef>

namespace utils {

template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "RingBuffer size must be a power of two");

public:
    // --- Producer (vd. ISR) ---
    bool push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) return false;   // đầy
        buf_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // --- Consumer (vd. thread) ---
    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;   // rỗng
        out = buf_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    std::size_t pop(T* out, std::size_t max) {
        std::size_t n = 0;
        while (n < max && pop(out[n])) ++n;
        return n;
    }

    // Giá trị xấp xỉ khi gọi đồng thời với thread kia – chỉ dùng để thống kê.
    std::size_t size() const {
        return (head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire)) & kMask;
    }
    bool empty() const { return size() == 0; }
    static constexpr std::size_t capacity() { return N - 1; }

private:
    static constexpr std::size_t kMask = N - 1;
    std::array<T, N> buf_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

}  // namespace utils
