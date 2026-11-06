#pragma once
// Chính sách bộ nhớ của framework: KHÔNG cấp phát động sau giai đoạn khởi động.
//
//  1. Khởi động (startup): được phép lấy bộ nhớ một lần – stack cho thread từ vùng stack tĩnh
//     (allocate_stack), object static, và (trên host) cấp phát nội bộ của thư viện chuẩn.
//  2. end_startup(): niêm phong. Từ đây:
//       - allocate_stack() luôn thất bại (thread mới phải tự cung cấp stack tĩnh),
//       - mọi lần gọi heap (operator new/malloc, nếu bộ chặn heap được link) bị coi là VI PHẠM:
//         tăng bộ đếm và gọi HeapViolationHandler do target cài (mặc định: abort).
//
// Payload lớn dùng svc::Buffer + PoolAllocator: bộ nhớ là pool TĨNH, cấp/trả O(1) –
// không phải heap, được phép lúc chạy.
#include <cstddef>
#include <cstdint>

#include "osal/types.hpp"

namespace osal {

// ---------------------------------------------------------- vùng stack tĩnh
struct StackPoolStats {
    std::size_t capacity;    // byte
    std::size_t used;        // byte
    std::uint32_t failures;  // số lần cấp thất bại (hết chỗ hoặc sau khi niêm phong)
};

// Cấp `bytes` byte (căn lề 8) cho stack thread. nullptr nếu hết chỗ hoặc đã niêm phong.
void* allocate_stack(std::size_t bytes);
StackPoolStats stack_pool_stats();

// ------------------------------------------------------ giai đoạn khởi động
void end_startup();
bool startup_complete();

using HeapViolationHandler = void (*)(std::size_t bytes);
void set_heap_violation_handler(HeapViolationHandler handler);
std::uint32_t heap_violations();

// Chỉ dùng trong unit test: quay lại giai đoạn khởi động.
void reset_startup_for_test();

// ---------------------------------------------------- danh sách thread đang chạy
struct ThreadInfo {
    const char* name;
    Priority priority;
    std::size_t stack_bytes;
    bool pooled_stack;   // stack lấy từ vùng stack tĩnh dùng chung
};

std::size_t thread_count();
bool thread_info(std::size_t index, ThreadInfo& out);

namespace detail {
void note_heap_allocation(std::size_t bytes);   // gọi bởi bộ chặn heap
void register_thread(const void* thread, const ThreadInfo& info);
void unregister_thread(const void* thread);

// Miễn kiểm tra heap cho cấp phát NỘI BỘ của port trên host (vd. std::thread), vốn không tồn tại
// trên MCU. Chỉ áp dụng cho thread đang giữ đối tượng này, trong phạm vi của nó.
class HostAllocationExemption {
public:
    HostAllocationExemption();
    ~HostAllocationExemption();
    HostAllocationExemption(const HostAllocationExemption&)            = delete;
    HostAllocationExemption& operator=(const HostAllocationExemption&) = delete;
};
}  // namespace detail

}  // namespace osal
