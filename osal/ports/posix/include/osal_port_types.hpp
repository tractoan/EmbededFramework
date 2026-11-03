#pragma once
// Kiểu native cho port POSIX (Linux/macOS) – dùng thư viện chuẩn C++.
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace osal::port {

struct MutexHandle {
    std::timed_mutex m;
};

struct SemaphoreHandle {
    std::mutex m;
    std::condition_variable cv;
    std::uint32_t count = 0;
    std::uint32_t max   = 1;
};

struct ThreadHandle {
    std::thread t;
};

}  // namespace osal::port
