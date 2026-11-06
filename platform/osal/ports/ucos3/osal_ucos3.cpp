// Port OSAL cho Micrium uC/OS-III (V3.0x) + uC/CPU.
//
// Cấu hình os_cfg.h tối thiểu:
//   OS_CFG_SEM_EN, OS_CFG_MUTEX_EN, OS_CFG_TASK_DEL_EN, OS_CFG_TASK_SUSPEND_EN = 1
//   OS_CFG_SEM_DEL_EN, OS_CFG_MUTEX_DEL_EN   (tùy chọn: hủy object khi destructor chạy)
//   OS_CFG_SCHED_ROUND_ROBIN_EN              (tùy chọn: yield() nhường task cùng priority)
// Đặc thù uC/OS-III được xử lý ở đây:
//   - Timeout 0 + OS_OPT_PEND_BLOCKING = CHỜ MÃI; "không chờ" dùng OS_OPT_PEND_NON_BLOCKING.
//   - Nhiều task được trùng priority (round-robin) -> mỗi mức osal::Priority là một priority cố định
//     (EP_UCOS3_PRIO_*), số nhỏ = ưu tiên cao.
//   - OS_TCB nằm trong Thread: task kết thúc sẽ tự treo, join() xóa task rồi mới trả về.
//   - Không cấp phát stack động -> stack_mem == nullptr thì lấy từ vùng stack tĩnh
//     EP_UCOS_STACK_POOL_BYTES (cấp một chiều, không thu hồi).
//   - Mọi object OSAL phải được tạo SAU OSInit() (target::init gọi OSInit đầu tiên).
#include <cstdint>

#include "osal/osal.hpp"

#ifndef EP_UCOS_STACK_POOL_BYTES
#define EP_UCOS_STACK_POOL_BYTES (24u * 1024u)
#endif
#ifndef EP_UCOS3_PRIO_REALTIME
#define EP_UCOS3_PRIO_REALTIME 4u
#endif
#ifndef EP_UCOS3_PRIO_HIGH
#define EP_UCOS3_PRIO_HIGH 6u
#endif
#ifndef EP_UCOS3_PRIO_NORMAL
#define EP_UCOS3_PRIO_NORMAL 8u
#endif
#ifndef EP_UCOS3_PRIO_LOW
#define EP_UCOS3_PRIO_LOW 10u
#endif
#ifndef EP_UCOS3_PRIO_IDLE
#define EP_UCOS3_PRIO_IDLE (OS_CFG_PRIO_MAX - 3u)   // trên idle task (OS_CFG_PRIO_MAX - 1)
#endif

namespace osal {

namespace {

CPU_INT32U tick_rate() { return OSCfg_TickRate_Hz; }

OS_TICK to_ticks(std::uint32_t ms) {
    std::uint64_t t = (static_cast<std::uint64_t>(ms) * tick_rate() + 999u) / 1000u;
    if (t == 0) t = 1;                      // 0 = chờ mãi -> tối thiểu 1 tick
    if (t > 0xFFFFFFFEu) t = 0xFFFFFFFEu;
    return static_cast<OS_TICK>(t);
}

bool sem_take(OS_SEM* s, std::uint32_t timeout_ms) {
    OS_ERR err = OS_ERR_NONE;
    if (timeout_ms == kNoWait) {
        (void)OSSemPend(s, 0u, OS_OPT_PEND_NON_BLOCKING, nullptr, &err);
    } else {
        (void)OSSemPend(s, timeout_ms == kWaitForever ? 0u : to_ticks(timeout_ms), OS_OPT_PEND_BLOCKING, nullptr, &err);
    }
    return err == OS_ERR_NONE;
}

alignas(8) CPU_STK g_stack_pool[EP_UCOS_STACK_POOL_BYTES / sizeof(CPU_STK)];
std::size_t g_stack_used = 0;

CPU_STK* alloc_stack(std::size_t count) {
    CriticalSection cs;
    count = (count + 1u) & ~std::size_t{1u};
    if (count > sizeof(g_stack_pool) / sizeof(CPU_STK) - g_stack_used) return nullptr;
    CPU_STK* p = &g_stack_pool[g_stack_used];
    g_stack_used += count;
    return p;
}

OS_PRIO to_native(Priority p) {
    switch (p) {
        case Priority::Realtime: return EP_UCOS3_PRIO_REALTIME;
        case Priority::High:     return EP_UCOS3_PRIO_HIGH;
        case Priority::Normal:   return EP_UCOS3_PRIO_NORMAL;
        case Priority::Low:      return EP_UCOS3_PRIO_LOW;
        case Priority::Idle:     return EP_UCOS3_PRIO_IDLE;
    }
    return EP_UCOS3_PRIO_NORMAL;
}

CPU_CHAR* name_of(const char* n) { return reinterpret_cast<CPU_CHAR*>(const_cast<char*>(n ? n : "ep")); }

}  // namespace

// ----------------------------------------------------------------- Mutex
Mutex::Mutex() {
    OS_ERR err = OS_ERR_NONE;
    OSMutexCreate(&h_.mutex, name_of("osal_mutex"), &err);
}

Mutex::~Mutex() {
#if OS_CFG_MUTEX_DEL_EN > 0u
    OS_ERR err = OS_ERR_NONE;
    (void)OSMutexDel(&h_.mutex, OS_OPT_DEL_ALWAYS, &err);
#endif
}

void Mutex::lock() { (void)try_lock(kWaitForever); }

bool Mutex::try_lock(std::uint32_t timeout_ms) {
    OS_ERR err = OS_ERR_NONE;
    if (timeout_ms == kNoWait) {
        OSMutexPend(&h_.mutex, 0u, OS_OPT_PEND_NON_BLOCKING, nullptr, &err);
    } else {
        OSMutexPend(&h_.mutex, timeout_ms == kWaitForever ? 0u : to_ticks(timeout_ms), OS_OPT_PEND_BLOCKING, nullptr,
                    &err);
    }
    return err == OS_ERR_NONE;
}

void Mutex::unlock() {
    OS_ERR err = OS_ERR_NONE;
    OSMutexPost(&h_.mutex, OS_OPT_POST_NONE, &err);
}

// ------------------------------------------------------------- Semaphore
Semaphore::Semaphore(std::uint32_t initial, std::uint32_t max) {
    OS_ERR err = OS_ERR_NONE;
    h_.max = static_cast<OS_SEM_CTR>(max);
    OSSemCreate(&h_.sem, name_of("osal_sem"), static_cast<OS_SEM_CTR>(initial > max ? max : initial), &err);
}

Semaphore::~Semaphore() {
#if OS_CFG_SEM_DEL_EN > 0u
    OS_ERR err = OS_ERR_NONE;
    (void)OSSemDel(&h_.sem, OS_OPT_DEL_ALWAYS, &err);
#endif
}

bool Semaphore::take(std::uint32_t timeout_ms) { return sem_take(&h_.sem, timeout_ms); }

void Semaphore::give() {
    CriticalSection cs;   // kiểm tra + post nguyên khối để giữ ngữ nghĩa "không vượt max"
    if (h_.sem.Ctr < h_.max) {
        OS_ERR err = OS_ERR_NONE;
        (void)OSSemPost(&h_.sem, OS_OPT_POST_1, &err);
    }
}

void Semaphore::give_from_isr() { give(); }   // OSSemPost gọi được từ ISR (ISR bọc OSIntEnter/OSIntExit)

// ---------------------------------------------------------------- Thread
Thread::~Thread() { join(); }

core::Status Thread::start(const ThreadConfig& cfg, Entry entry, void* arg) {
    if (started_ || entry == nullptr) return core::Status::InvalidArg;
    const std::size_t n = cfg.stack_bytes / sizeof(CPU_STK);
    if (n < 64u) return core::Status::InvalidArg;
    CPU_STK* stk = cfg.stack_mem != nullptr ? static_cast<CPU_STK*>(cfg.stack_mem) : alloc_stack(n);
    if (stk == nullptr) return core::Status::NoMemory;

    entry_     = entry;
    arg_       = arg;
    OS_ERR err = OS_ERR_NONE;
    OSSemCreate(&h_.done, name_of("osal_join"), 0u, &err);
    if (err != OS_ERR_NONE) return core::Status::NoMemory;

    OSTaskCreate(&h_.tcb, name_of(cfg.name), &Thread::trampoline, this, to_native(cfg.priority), stk,
                 static_cast<CPU_STK_SIZE>(n / 10u), static_cast<CPU_STK_SIZE>(n), 0u, 0u, nullptr,
                 OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR, &err);
    if (err != OS_ERR_NONE) {
#if OS_CFG_SEM_DEL_EN > 0u
        OS_ERR del_err = OS_ERR_NONE;
        (void)OSSemDel(&h_.done, OS_OPT_DEL_ALWAYS, &del_err);
#endif
        return core::Status::Error;
    }
    started_ = true;
    return core::Status::Ok;
}

void Thread::join() {
    if (!started_) return;
    OS_ERR err = OS_ERR_NONE;
    (void)OSSemPend(&h_.done, 0u, OS_OPT_PEND_BLOCKING, nullptr, &err);
    OSTaskDel(&h_.tcb, &err);   // task đã xong (đang treo hoặc sắp treo) -> xóa trước khi TCB bị hủy
#if OS_CFG_SEM_DEL_EN > 0u
    (void)OSSemDel(&h_.done, OS_OPT_DEL_ALWAYS, &err);
#endif
    started_ = false;
}

void Thread::trampoline(void* self) {
    auto* t = static_cast<Thread*>(self);
    t->entry_(t->arg_);
    OS_ERR err = OS_ERR_NONE;
    (void)OSSemPost(&t->h_.done, OS_OPT_POST_1, &err);
    // Không tự xóa: TCB thuộc về Thread, join() sẽ xóa. Task uC/OS-III không được return.
    for (;;) OSTaskSuspend(nullptr, &err);
}

// ------------------------------------------------------------------ Time
std::uint64_t now_ms() {
    static OS_TICK last        = 0;
    static std::uint64_t high  = 0;
    std::uint64_t ticks;
    {
        CriticalSection cs;
        OS_ERR err      = OS_ERR_NONE;
        const OS_TICK t = OSTimeGet(&err);
        if (t < last) high += (1ull << 32);
        last  = t;
        ticks = high | t;
    }
    return ticks * 1000u / tick_rate();
}

void sleep_ms(std::uint32_t ms) {
    if (ms == 0) {
        yield();
        return;
    }
    OS_ERR err = OS_ERR_NONE;
    OSTimeDly(to_ticks(ms), OS_OPT_TIME_DLY, &err);
}

void yield() {
    OS_ERR err = OS_ERR_NONE;
#if OS_CFG_SCHED_ROUND_ROBIN_EN > 0u
    OSSchedRoundRobinYield(&err);
#else
    OSTimeDly(1u, OS_OPT_TIME_DLY, &err);
#endif
}

// ------------------------------------------------------- CriticalSection
CriticalSection::CriticalSection() { state_.sr = CPU_SR_Save(); }
CriticalSection::~CriticalSection() { CPU_SR_Restore(state_.sr); }

}  // namespace osal
