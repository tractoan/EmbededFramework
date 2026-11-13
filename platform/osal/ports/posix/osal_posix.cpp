#include <chrono>
#include <cstring>

#if defined(__linux__) || defined(__QNX__)
#include <pthread.h>
#include <sched.h>
#endif

// EP_POSIX_REALTIME: mức 0..15 -> SCHED_FIFO EP_POSIX_RT_PRIO_BASE + mức * EP_POSIX_RT_PRIO_STEP.
// Mặc định 10..85: Linux – chừa chỗ cho IRQ thread của kernel (50) và tác vụ hệ thống;
// QNX – dải 1..255, mức > 63 cần quyền PROCMGR_AID_PRIORITY (root); driver của BSP thường ở 21..30.
#ifndef EP_POSIX_RT_PRIO_BASE
#define EP_POSIX_RT_PRIO_BASE 10
#endif
#ifndef EP_POSIX_RT_PRIO_STEP
#define EP_POSIX_RT_PRIO_STEP 5
#endif

#include "osal/memory.hpp"
#include "osal/osal.hpp"

namespace osal {

namespace {
using Clock = std::chrono::steady_clock;
const Clock::time_point g_epoch = Clock::now();

std::recursive_mutex& critical_mutex() {
    static std::recursive_mutex m;
    return m;
}
}  // namespace

// ----------------------------------------------------------------- Mutex
Mutex::Mutex()  = default;
Mutex::~Mutex() = default;

void Mutex::lock() { h_.m.lock(); }

bool Mutex::try_lock(std::uint32_t timeout_ms) {
    if (timeout_ms == kWaitForever) {
        h_.m.lock();
        return true;
    }
    if (timeout_ms == kNoWait) return h_.m.try_lock();
    // Lưu ý công cụ: try_lock_for dùng pthread_mutex_clocklock, TSan của GCC 13 không chặn hàm này
    // -> báo nhầm "unlock of an unlocked mutex"/data race. Đúng khi chạy thật; code cần TSan kiểm tra
    // nên dùng kNoWait + thử lại (xem services/src/actuator.cpp).
    return h_.m.try_lock_for(std::chrono::milliseconds(timeout_ms));
}

void Mutex::unlock() { h_.m.unlock(); }

// ------------------------------------------------------------- Semaphore
Semaphore::Semaphore(std::uint32_t initial, std::uint32_t max) {
    h_.count = initial;
    h_.max   = max;
}
Semaphore::~Semaphore() = default;

bool Semaphore::take(std::uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lk(h_.m);
    auto available = [this] { return h_.count > 0; };
    if (timeout_ms == kWaitForever) {
        h_.cv.wait(lk, available);
    } else if (!h_.cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), available)) {
        return false;
    }
    --h_.count;
    return true;
}

void Semaphore::give() {
    {
        std::lock_guard<std::mutex> lk(h_.m);
        if (h_.count < h_.max) ++h_.count;
    }
    h_.cv.notify_one();
}

void Semaphore::give_from_isr() { give(); }

// ---------------------------------------------------------------- Thread
Thread::~Thread() { join(); }

core::Status Thread::start(const ThreadConfig& cfg, Entry entry, void* arg) {
    if (started_ || entry == nullptr) return core::Status::InvalidArg;
    // std::thread tự quản stack, nhưng vẫn trừ vào vùng stack tĩnh như trên MCU để bản mô phỏng
    // phát hiện cùng lỗi: hết vùng stack, hoặc tạo thread không có stack sau khi khởi động xong.
    const bool pooled = cfg.stack_mem == nullptr;
    if (pooled && allocate_stack(cfg.stack_bytes) == nullptr) return core::Status::NoMemory;
    entry_   = entry;
    arg_     = arg;
    {
        detail::HostAllocationExemption host_only;   // std::thread cấp phát nội bộ – không có trên MCU
        h_.t = std::thread(&Thread::trampoline, this);
    }
    started_ = true;
#if defined(__linux__) || defined(__QNX__)
    char name[16] = {};   // Linux: tối đa 15 ký tự; QNX cho phép dài hơn
    std::strncpy(name, cfg.name ? cfg.name : "ep", sizeof(name) - 1);
    pthread_setname_np(h_.t.native_handle(), name);
#else
    (void)cfg;
#endif
#if defined(EP_POSIX_REALTIME) && (defined(__linux__) || defined(__QNX__))
    // Linux/QNX thật: không có quyền thì OS từ chối, thread giữ chính sách mặc định (target in cảnh báo).
    sched_param sp{};
    sp.sched_priority = EP_POSIX_RT_PRIO_BASE + EP_POSIX_RT_PRIO_STEP * cfg.priority.level();
    pthread_setschedparam(h_.t.native_handle(), SCHED_FIFO, &sp);
#endif
    // Không bật EP_POSIX_REALTIME (mô phỏng/test trên host): priority chỉ được ghi nhận để hiển thị.
    detail::register_thread(this, ThreadInfo{cfg.name ? cfg.name : "ep", cfg.priority, cfg.stack_bytes, pooled});
    return core::Status::Ok;
}

void Thread::join() {
    if (started_ && h_.t.joinable()) h_.t.join();
    if (started_) detail::unregister_thread(this);
    started_ = false;
}

void Thread::trampoline(void* self) {
    auto* t = static_cast<Thread*>(self);
    t->entry_(t->arg_);
}

// ------------------------------------------------------------------ Time
std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_epoch).count());
}

void sleep_ms(std::uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void yield() { std::this_thread::yield(); }

// ------------------------------------------------------- CriticalSection
CriticalSection::CriticalSection() { critical_mutex().lock(); }
CriticalSection::~CriticalSection() { critical_mutex().unlock(); }

}  // namespace osal
