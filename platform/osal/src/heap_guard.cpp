// Bộ chặn heap: thay operator new/delete toàn cục (và malloc/calloc/realloc nếu target bật
// EP_HEAP_GUARD_WRAP_MALLOC kèm -Wl,--wrap=malloc,...). Mỗi lần cấp phát sau osal::end_startup()
// là một vi phạm -> osal::detail::note_heap_allocation() -> HeapViolationHandler của target.
//
// Được build thành OBJECT library (ep_heap_guard) và link thẳng vào firmware để chắc chắn
// bản thay thế operator new được dùng.
#include <cstdlib>
#include <new>

#include "osal/memory.hpp"

#if defined(EP_HEAP_GUARD_WRAP_MALLOC)
extern "C" {
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t, std::size_t);
void* __real_realloc(void*, std::size_t);

void* __wrap_malloc(std::size_t n) {
    osal::detail::note_heap_allocation(n);
    return __real_malloc(n);
}
void* __wrap_calloc(std::size_t n, std::size_t size) {
    osal::detail::note_heap_allocation(n * size);
    return __real_calloc(n, size);
}
void* __wrap_realloc(void* p, std::size_t n) {
    osal::detail::note_heap_allocation(n);
    return __real_realloc(p, n);
}
}
#define EP_RAW_MALLOC __real_malloc
#else
#define EP_RAW_MALLOC std::malloc
#endif

namespace {
void* guarded_alloc(std::size_t n) {
    osal::detail::note_heap_allocation(n);
    return EP_RAW_MALLOC(n != 0 ? n : 1);
}
void* guarded_alloc_or_abort(std::size_t n) {
    void* p = guarded_alloc(n);
    if (p == nullptr) std::abort();   // -fno-exceptions: không ném std::bad_alloc
    return p;
}
}  // namespace

void* operator new(std::size_t n) { return guarded_alloc_or_abort(n); }
void* operator new[](std::size_t n) { return guarded_alloc_or_abort(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return guarded_alloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return guarded_alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
