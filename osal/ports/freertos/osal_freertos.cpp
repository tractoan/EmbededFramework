// Port OSAL cho FreeRTOS (yêu cầu configSUPPORT_STATIC_ALLOCATION = 1).
#include "osal/osal.hpp"

namespace osal {

namespace {
TickType_t to_ticks(std::uint32_t ms) {
    return ms == kWaitForever ? portMAX_DELAY : pdMS_TO_TICKS(ms);
}

UBaseType_t to_native(Priority p) {
    switch (p) {
        case Priority::Idle:     return tskIDLE_PRIORITY;
        case Priority::Low:      return tskIDLE_PRIORITY + 1;
        case Priority::Normal:   return tskIDLE_PRIORITY + 2;
        case Priority::High:     return tskIDLE_PRIORITY + 3;
        case Priority::Realtime: return configMAX_PRIORITIES - 1;
    }
    return tskIDLE_PRIORITY + 2;
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

    const auto depth = static_cast<configSTACK_DEPTH_TYPE>(cfg.stack_bytes / sizeof(StackType_t));
    if (cfg.stack_mem != nullptr) {
        h_.handle = xTaskCreateStatic(&Thread::trampoline, cfg.name, depth, this, to_native(cfg.priority),
                                      static_cast<StackType_t*>(cfg.stack_mem), &h_.tcb);
        if (h_.handle == nullptr) return core::Status::Error;
    } else if (xTaskCreate(&Thread::trampoline, cfg.name, depth, this, to_native(cfg.priority), &h_.handle) != pdPASS) {
        return core::Status::NoMemory;
    }
    started_ = true;
    return core::Status::Ok;
}

void Thread::join() {
    if (!started_) return;
    xSemaphoreTake(h_.done, portMAX_DELAY);
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
