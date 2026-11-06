// Port OSAL cho Micrium uC/OS-II (V2.9x).
//
// Cấu hình os_cfg.h tối thiểu:
//   OS_SEM_EN, OS_SEM_ACCEPT_EN, OS_TASK_CREATE_EXT_EN, OS_TASK_DEL_EN, OS_TIME_GET_SET_EN = 1
//   OS_MUTEX_EN, OS_MUTEX_ACCEPT_EN = 1   (khuyến nghị; nếu tắt, Mutex dùng semaphore nhị phân)
//   OS_SEM_DEL_EN, OS_MUTEX_DEL_EN        (tùy chọn: hủy object khi destructor chạy)
//   OS_CRITICAL_METHOD = 3                (os_cpu.h của port CPU)
// Đặc thù uC/OS-II được xử lý ở đây:
//   - Mỗi task phải có priority DUY NHẤT -> mỗi mức logic (0..15) là một dải EP_UCOS2_PRIO_STRIDE
//     priority native liên tiếp, bắt đầu từ EP_UCOS2_PRIO_TOP cho mức 15; port lấy priority trống
//     đầu tiên trong dải (tối đa EP_UCOS2_PRIO_STRIDE task cùng mức).
//   - Timeout 0 của OSxxxPend nghĩa là CHỜ MÃI -> "không chờ" dùng OSSemAccept/OSMutexAccept.
//   - Không cấp phát stack động -> ThreadConfig::stack_mem == nullptr thì lấy từ vùng stack tĩnh
//     chung của OSAL (osal::allocate_stack, chỉ trong giai đoạn khởi động).
//   - Mọi object OSAL phải được tạo SAU OSInit() (target::init gọi OSInit đầu tiên).
#include <cstdint>

#include "osal/memory.hpp"
#include "osal/osal.hpp"

#if OS_CRITICAL_METHOD != 3u
#error "OSAL uC/OS-II port requires OS_CRITICAL_METHOD == 3"
#endif

#ifndef EP_UCOS2_PRIO_TOP
#define EP_UCOS2_PRIO_TOP 4u      // priority native của mức logic cao nhất (0..3 dành cho hệ thống)
#endif
#ifndef EP_UCOS2_PRIO_STRIDE
#define EP_UCOS2_PRIO_STRIDE 3u   // số priority native mỗi mức logic = số task tối đa cùng mức
#endif
#if EP_UCOS2_PRIO_TOP + 16u * EP_UCOS2_PRIO_STRIDE > OS_LOWEST_PRIO - 1u
#error "EP_UCOS2_PRIO_TOP/STRIDE vượt quá OS_LOWEST_PRIO: tăng OS_LOWEST_PRIO hoặc giảm STRIDE"
#endif

#if OS_MUTEX_EN > 0u && OS_MUTEX_ACCEPT_EN > 0u && defined(OS_PRIO_MUTEX_CEIL_DIS)
#define EP_UCOS2_USE_MUTEX 1
#else
#define EP_UCOS2_USE_MUTEX 0   // uC/OS-II cũ: mutex cần một priority PIP riêng -> dùng semaphore nhị phân
#endif

namespace osal {

namespace {

INT32U to_ticks(std::uint32_t ms) {
    std::uint64_t t = (static_cast<std::uint64_t>(ms) * OS_TICKS_PER_SEC + 999u) / 1000u;
    if (t == 0) t = 1;                      // 0 = chờ mãi trong uC/OS-II -> tối thiểu 1 tick
    if (t > 0xFFFFFFFEu) t = 0xFFFFFFFEu;
    return static_cast<INT32U>(t);
}

INT32U pend_ticks(std::uint32_t timeout_ms) { return timeout_ms == kWaitForever ? 0u : to_ticks(timeout_ms); }

bool sem_take(OS_EVENT* ev, std::uint32_t timeout_ms) {
    if (timeout_ms == kNoWait) return OSSemAccept(ev) > 0u;
    INT8U err = OS_ERR_NONE;
    OSSemPend(ev, pend_ticks(timeout_ms), &err);
    return err == OS_ERR_NONE;
}

// Mức logic 15 -> EP_UCOS2_PRIO_TOP, mỗi mức thấp hơn lùi EP_UCOS2_PRIO_STRIDE (số nhỏ = ưu tiên cao).
INT8U band_base(Priority p) {
    return static_cast<INT8U>(EP_UCOS2_PRIO_TOP + (Priority::kMax - p.level()) * EP_UCOS2_PRIO_STRIDE);
}

}  // namespace

// ----------------------------------------------------------------- Mutex
Mutex::Mutex() {
#if EP_UCOS2_USE_MUTEX
    INT8U err = OS_ERR_NONE;
    h_.event  = OSMutexCreate(OS_PRIO_MUTEX_CEIL_DIS, &err);
#else
    h_.event = OSSemCreate(1u);
#endif
}

Mutex::~Mutex() {
    INT8U err = OS_ERR_NONE;
#if EP_UCOS2_USE_MUTEX && OS_MUTEX_DEL_EN > 0u
    if (h_.event != nullptr) OSMutexDel(h_.event, OS_DEL_ALWAYS, &err);
#elif !EP_UCOS2_USE_MUTEX && OS_SEM_DEL_EN > 0u
    if (h_.event != nullptr) OSSemDel(h_.event, OS_DEL_ALWAYS, &err);
#endif
    (void)err;
}

void Mutex::lock() { (void)try_lock(kWaitForever); }

bool Mutex::try_lock(std::uint32_t timeout_ms) {
#if EP_UCOS2_USE_MUTEX
    INT8U err = OS_ERR_NONE;
    if (timeout_ms == kNoWait) return OSMutexAccept(h_.event, &err) == OS_TRUE;
    OSMutexPend(h_.event, pend_ticks(timeout_ms), &err);
    return err == OS_ERR_NONE;
#else
    return sem_take(h_.event, timeout_ms);
#endif
}

void Mutex::unlock() {
#if EP_UCOS2_USE_MUTEX
    (void)OSMutexPost(h_.event);
#else
    (void)OSSemPost(h_.event);
#endif
}

// ------------------------------------------------------------- Semaphore
Semaphore::Semaphore(std::uint32_t initial, std::uint32_t max) {
    h_.max   = static_cast<INT16U>(max > 0xFFFFu ? 0xFFFFu : max);
    h_.event = OSSemCreate(static_cast<INT16U>(initial > h_.max ? h_.max : initial));
}

Semaphore::~Semaphore() {
#if OS_SEM_DEL_EN > 0u
    INT8U err = OS_ERR_NONE;
    if (h_.event != nullptr) OSSemDel(h_.event, OS_DEL_ALWAYS, &err);
#endif
}

bool Semaphore::take(std::uint32_t timeout_ms) { return sem_take(h_.event, timeout_ms); }

void Semaphore::give() {
    // Giữ ngữ nghĩa "không vượt max" như các port khác; kiểm tra + post trong cùng vùng găng.
    CriticalSection cs;
    if (h_.event->OSEventCnt < h_.max) (void)OSSemPost(h_.event);
}

void Semaphore::give_from_isr() { give(); }   // OSSemPost gọi được từ ISR (ISR bọc OSIntEnter/OSIntExit)

// ---------------------------------------------------------------- Thread
Thread::~Thread() { join(); }

core::Status Thread::start(const ThreadConfig& cfg, Entry entry, void* arg) {
    if (started_ || entry == nullptr) return core::Status::InvalidArg;
    const std::size_t n = cfg.stack_bytes / sizeof(OS_STK);
    if (n < 64u) return core::Status::InvalidArg;
    const bool pooled = cfg.stack_mem == nullptr;
    auto* stk = static_cast<OS_STK*>(pooled ? allocate_stack(n * sizeof(OS_STK)) : cfg.stack_mem);
    if (stk == nullptr) return core::Status::NoMemory;

    entry_  = entry;
    arg_    = arg;
    h_.done = OSSemCreate(0u);
    if (h_.done == nullptr) return core::Status::NoMemory;

#if OS_STK_GROWTH == 1u
    OS_STK* const ptos = &stk[n - 1u];
    OS_STK* const pbos = &stk[0];
#else
    OS_STK* const ptos = &stk[0];
    OS_STK* const pbos = &stk[n - 1u];
#endif

    const INT8U base = band_base(cfg.priority);
    for (INT8U p = base; p < base + EP_UCOS2_PRIO_STRIDE; ++p) {
        const INT8U err = OSTaskCreateExt(&Thread::trampoline, this, ptos, p, p, pbos, static_cast<INT32U>(n), nullptr,
                                          OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR);
        if (err == OS_ERR_NONE) {
            h_.prio  = p;
            started_ = true;
            detail::register_thread(this, ThreadInfo{cfg.name ? cfg.name : "ep", cfg.priority, cfg.stack_bytes, pooled});
#if OS_TASK_NAME_EN > 0u
            INT8U name_err = OS_ERR_NONE;
            OSTaskNameSet(p, reinterpret_cast<INT8U*>(const_cast<char*>(cfg.name ? cfg.name : "ep")), &name_err);
#endif
            return core::Status::Ok;
        }
        if (err != OS_ERR_PRIO_EXIST) break;   // lỗi khác priority trùng: không thử tiếp
    }
#if OS_SEM_DEL_EN > 0u
    INT8U del_err = OS_ERR_NONE;
    OSSemDel(h_.done, OS_DEL_ALWAYS, &del_err);
#endif
    h_.done = nullptr;
    return core::Status::Busy;   // hết priority trống trong dải
}

void Thread::join() {
    if (!started_) return;
    INT8U err = OS_ERR_NONE;
    OSSemPend(h_.done, 0u, &err);   // 0 = chờ mãi
#if OS_SEM_DEL_EN > 0u
    OSSemDel(h_.done, OS_DEL_ALWAYS, &err);
#endif
    h_.done  = nullptr;
    detail::unregister_thread(this);
    started_ = false;
}

void Thread::trampoline(void* self) {
    auto* t = static_cast<Thread*>(self);
    t->entry_(t->arg_);
    OS_EVENT* done = t->h_.done;   // sau khi post, Thread có thể bị hủy -> không chạm vào t nữa
    (void)OSSemPost(done);
    (void)OSTaskDel(OS_PRIO_SELF);   // task uC/OS-II không được return
    for (;;) {}
}

// ------------------------------------------------------------------ Time
std::uint64_t now_ms() {
    // Mở rộng bộ đếm tick 32-bit thành 64-bit (cần gọi ít nhất 1 lần mỗi chu kỳ tràn).
    static INT32U last          = 0;
    static std::uint64_t high   = 0;
    std::uint64_t ticks;
    {
        CriticalSection cs;
        const INT32U t = OSTimeGet();
        if (t < last) high += (1ull << 32);
        last  = t;
        ticks = high | t;
    }
    return ticks * 1000u / OS_TICKS_PER_SEC;
}

void sleep_ms(std::uint32_t ms) {
    if (ms == 0) {
        yield();
        return;
    }
    OSTimeDly(to_ticks(ms));
}

// uC/OS-II không có task cùng priority nên không có "yield" thực sự: nhường CPU 1 tick.
void yield() { OSTimeDly(1u); }

// ------------------------------------------------------- CriticalSection
CriticalSection::CriticalSection() { state_.sr = OS_CPU_SR_Save(); }
CriticalSection::~CriticalSection() { OS_CPU_SR_Restore(state_.sr); }

}  // namespace osal
