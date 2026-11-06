// Port OSAL cho FreeRTOS – chỉ dùng API cấp phát TĨNH (configSUPPORT_STATIC_ALLOCATION = 1,
// có thể đặt configSUPPORT_DYNAMIC_ALLOCATION = 0 và bỏ heap_x.c khỏi build).
#include "osal/memory.hpp"
#include "osal/osal.hpp"

namespace osal {

namespace {
TickType_t to_ticks(std::uint32_t ms) {
    return ms == kWaitForever ? portMAX_DELAY : pdMS_TO_TICKS(ms);
}

// Mức logic 0..15 -> tskIDLE_PRIORITY..configMAX_PRIORITIES-1, tuyến tính (số lớn = ưu tiên cao).
UBaseType_t to_native(Priority p) {
    constexpr UBaseType_t span = (configMAX_PRIORITIES - 1u) - tskIDLE_PRIORITY;
    return tskIDLE_PRIORITY + (span * p.level() + Priority::kMax / 2u) / Priority::kMax;
}
}  // namespace

// ----------------------------------------------------------------- Mutex
Mutex::Mutex() { h_.handle = xSemaphoreCreateMutexStatic(&h_.storage); }
Mutex::~Mutex() { vSemaphoreDelete(h_.handle); }
void Mutex::lock() { xSemaphoreTake(h_.handle, portMAX_DELAY); }
bool Mutex::try_lock(std::uint32_t timeout_ms) { return xSemaphoreTake(h_.handle, to_ticks(timeout_ms)) == pdTRUE; }
void Mutex::unlock() { xSemaphoreGive(h_.handle); }

// ------------------------------------------------------------- Semaphore
Semaphore::Semaphore(std::uint32_t initial, std::uint32_t max) {
    h_.handle = xSemaphoreCreateCountingStatic(max, initial, &h_.storage);
}
Semaphore::~Semaphore() { vSemaphoreDelete(h_.handle); }
bool Semaphore::take(std::uint32_t timeout_ms) { return xSemaphoreTake(h_.handle, to_ticks(timeout_ms)) == pdTRUE; }
void Semaphore::give() { xSemaphoreGive(h_.handle); }
void Semaphore::give_from_isr() {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(h_.handle, &woken);
    portYIELD_FROM_ISR(woken);
}

// ---------------------------------------------------------------- Thread
Thread::~Thread() { join(); }

core::Status Thread::start(const ThreadConfig& cfg, Entry entry, void* arg) {
    if (started_ || entry == nullptr) return core::Status::InvalidArg;
    entry_  = entry;
    arg_    = arg;
    h_.done = xSemaphoreCreateBinaryStatic(&h_.done_storage);

    // Không bao giờ dùng heap FreeRTOS: stack do người gọi cấp, hoặc lấy từ vùng stack tĩnh (chỉ khi khởi động).
    const bool pooled = cfg.stack_mem == nullptr;
    void* stack       = pooled ? allocate_stack(cfg.stack_bytes) : cfg.stack_mem;
    if (stack == nullptr) return core::Status::NoMemory;
    const auto depth = static_cast<configSTACK_DEPTH_TYPE>(cfg.stack_bytes / sizeof(StackType_t));
    h_.handle = xTaskCreateStatic(&Thread::trampoline, cfg.name, depth, this, to_native(cfg.priority),
                                  static_cast<StackType_t*>(stack), &h_.tcb);
    if (h_.handle == nullptr) return core::Status::Error;
    started_ = true;
    detail::register_thread(this, ThreadInfo{cfg.name ? cfg.name : "ep", cfg.priority, cfg.stack_bytes, pooled});
    return core::Status::Ok;
}

void Thread::join() {
    if (!started_) return;
    xSemaphoreTake(h_.done, portMAX_DELAY);
    detail::unregister_thread(this);
    started_ = false;
}

void Thread::trampoline(void* self) {
    auto* t = static_cast<Thread*>(self);
    t->entry_(t->arg_);
    xSemaphoreGive(t->h_.done);
    vTaskDelete(nullptr);
}

// ------------------------------------------------------------------ Time
std::uint64_t now_ms() {
    // Lưu ý: TickType_t 32-bit sẽ tràn sau ~49 ngày @1kHz; dùng hook tick để mở rộng nếu cần.
    return static_cast<std::uint64_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
}
void sleep_ms(std::uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
void yield() { taskYIELD(); }

// ------------------------------------------------------- CriticalSection
CriticalSection::CriticalSection() { taskENTER_CRITICAL(); }
CriticalSection::~CriticalSection() { taskEXIT_CRITICAL(); }

}  // namespace osal
