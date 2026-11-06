#include "osal/memory.hpp"

#include <array>
#include <atomic>
#include <cstdlib>

#include "core/config.hpp"
#include "osal/critical.hpp"

namespace osal {

namespace {
// Vùng stack tĩnh dùng chung (cấp một chiều trong giai đoạn khởi động, không thu hồi).
alignas(8) std::uint8_t g_stack_pool[core::cfg::kStackPoolBytes];
std::size_t g_stack_used       = 0;
std::uint32_t g_stack_failures = 0;

std::atomic<bool> g_startup_complete{false};
std::atomic<std::uint32_t> g_heap_violations{0};
std::atomic<HeapViolationHandler> g_handler{nullptr};

thread_local int t_exemptions = 0;

struct Entry {
    const void* thread;
    ThreadInfo info;
};
std::array<Entry, core::cfg::kMaxThreads> g_threads{};
std::size_t g_thread_count = 0;
}  // namespace

void* allocate_stack(std::size_t bytes) {
    CriticalSection cs;
    bytes = (bytes + 7u) & ~std::size_t{7u};
    if (g_startup_complete.load() || bytes > sizeof(g_stack_pool) - g_stack_used) {
        ++g_stack_failures;
        return nullptr;
    }
    void* p = &g_stack_pool[g_stack_used];
    g_stack_used += bytes;
    return p;
}

StackPoolStats stack_pool_stats() {
    CriticalSection cs;
    return StackPoolStats{sizeof(g_stack_pool), g_stack_used, g_stack_failures};
}

void end_startup() { g_startup_complete.store(true); }
bool startup_complete() { return g_startup_complete.load(); }
void set_heap_violation_handler(HeapViolationHandler handler) { g_handler.store(handler); }
std::uint32_t heap_violations() { return g_heap_violations.load(); }
void reset_startup_for_test() { g_startup_complete.store(false); }

std::size_t thread_count() {
    CriticalSection cs;
    return g_thread_count;
}

bool thread_info(std::size_t index, ThreadInfo& out) {
    CriticalSection cs;
    if (index >= g_thread_count) return false;
    out = g_threads[index].info;
    return true;
}

namespace detail {

HostAllocationExemption::HostAllocationExemption() { ++t_exemptions; }
HostAllocationExemption::~HostAllocationExemption() { --t_exemptions; }

void note_heap_allocation(std::size_t bytes) {
    if (!g_startup_complete.load(std::memory_order_relaxed) || t_exemptions > 0) return;
    g_heap_violations.fetch_add(1);
    if (HeapViolationHandler h = g_handler.load()) {
        h(bytes);
    } else {
        std::abort();   // không có handler: dừng ngay để lỗi không bị bỏ qua
    }
}

void register_thread(const void* thread, const ThreadInfo& info) {
    CriticalSection cs;
    if (g_thread_count < g_threads.size()) g_threads[g_thread_count++] = Entry{thread, info};
}

void unregister_thread(const void* thread) {
    CriticalSection cs;
    for (std::size_t i = 0; i < g_thread_count; ++i) {
        if (g_threads[i].thread == thread) {
            g_threads[i] = g_threads[--g_thread_count];
            return;
        }
    }
}

}  // namespace detail

}  // namespace osal
