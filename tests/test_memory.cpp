// Chính sách bộ nhớ: vùng stack tĩnh, niêm phong sau khởi động, bộ chặn heap, danh sách thread, priority.
#include <atomic>
#include <cstring>
#include <new>

#include "osal/memory.hpp"
#include "osal/thread.hpp"
#include "test_framework.hpp"

namespace {
std::atomic<int> g_violations_seen{0};
std::size_t g_last_bytes = 0;
void count_violation(std::size_t bytes) {
    g_violations_seen.fetch_add(1);
    g_last_bytes = bytes;
}
void noop(void*) {}
alignas(8) std::uint8_t g_own_stack[2048];
}  // namespace

TEST(memory_priority_levels) {
    CHECK_EQ(osal::Priority::Idle.level(), 0u);
    CHECK(osal::Priority::Low < osal::Priority::Normal && osal::Priority::Normal < osal::Priority::High);
    CHECK_EQ(osal::Priority::Realtime.level(), osal::Priority::kMax);
    CHECK_EQ(osal::Priority{200}.level(), osal::Priority::kMax);   // chặn ở mức cao nhất
    CHECK_EQ(osal::Priority::High.lower(1).level(), 11u);
    CHECK_EQ(osal::Priority::Realtime.raise(3).level(), osal::Priority::kMax);
    CHECK_EQ(osal::Priority::Low.lower(10).level(), 0u);
    CHECK(osal::ThreadConfig{}.priority == osal::Priority::Normal);
}

TEST(memory_stack_pool_only_during_startup) {
    const osal::StackPoolStats before = osal::stack_pool_stats();
    void* a = osal::allocate_stack(100);   // làm tròn lên 104 (căn lề 8)
    CHECK(a != nullptr && reinterpret_cast<std::uintptr_t>(a) % 8 == 0);
    CHECK_EQ(osal::stack_pool_stats().used, before.used + 104);
    CHECK(osal::allocate_stack(before.capacity) == nullptr);   // không đủ chỗ

    osal::end_startup();
    CHECK(osal::allocate_stack(8) == nullptr);                  // sau khởi động: cấm
    CHECK_EQ(osal::stack_pool_stats().failures, before.failures + 2);

    osal::Thread late;   // thread không tự cấp stack sau khởi động -> từ chối trên MỌI port
    CHECK(late.start(osal::ThreadConfig{"late"}, &noop, nullptr) == core::Status::NoMemory);
    osal::Thread own;    // tự cung cấp stack tĩnh -> vẫn được phép
    CHECK(core::is_ok(own.start(osal::ThreadConfig{"own", osal::Priority::Low, sizeof(g_own_stack), g_own_stack},
                                &noop, nullptr)));
    own.join();
    osal::reset_startup_for_test();
}

TEST(memory_heap_guard_detects_allocation_after_startup) {
    osal::set_heap_violation_handler(&count_violation);
    const std::uint32_t before = osal::heap_violations();

    void* p = ::operator new(64);   // trong giai đoạn khởi động: được phép
    ::operator delete(p);
    CHECK_EQ(g_violations_seen.load(), 0);

    osal::end_startup();
    p = ::operator new(48);         // sau khởi động: vi phạm
    ::operator delete(p);
    void* q = ::operator new[](16, std::nothrow);
    ::operator delete[](q);
    osal::reset_startup_for_test();

    CHECK_EQ(g_violations_seen.load(), 2);
    CHECK_EQ(g_last_bytes, 16u);
    CHECK_EQ(osal::heap_violations(), before + 2);
    osal::set_heap_violation_handler(nullptr);
}

TEST(memory_thread_registry_reports_config) {
    const std::size_t n0 = osal::thread_count();
    osal::Thread t;
    CHECK(core::is_ok(t.start(osal::ThreadConfig{"probe7", osal::Priority{7}, sizeof(g_own_stack), g_own_stack},
                              &noop, nullptr)));
    CHECK_EQ(osal::thread_count(), n0 + 1);
    osal::ThreadInfo info{};
    bool found = false;
    for (std::size_t i = 0; osal::thread_info(i, info); ++i) {
        if (std::strcmp(info.name, "probe7") == 0) {
            found = true;
            CHECK_EQ(info.priority.level(), 7u);
            CHECK_EQ(info.stack_bytes, sizeof(g_own_stack));
            CHECK(!info.pooled_stack);
        }
    }
    CHECK(found);
    t.join();
    CHECK_EQ(osal::thread_count(), n0);
}
