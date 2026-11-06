#include "services/buffer.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#include "osal/critical.hpp"
#include "services/buffer_allocators.hpp"

namespace svc {

// ================================================================== Buffer
namespace {
std::atomic<IBufferAllocator*> g_default{nullptr};
}

void Buffer::set_default_allocator(IBufferAllocator* allocator) { g_default.store(allocator); }
IBufferAllocator* Buffer::default_allocator() { return g_default.load(); }

Buffer Buffer::allocate(std::size_t bytes) {
    IBufferAllocator* a = g_default.load();
    return a != nullptr ? allocate(*a, bytes) : Buffer{};
}

Buffer Buffer::allocate(IBufferAllocator& allocator, std::size_t bytes) {
    if (bytes > 0xFFFFFFFFu - kHeaderSize) return Buffer{};
    std::size_t cap = 0;
    void* block     = allocator.allocate(kHeaderSize + bytes, cap);
    if (block == nullptr) return Buffer{};
    Buffer b;
    b.h_ = new (block) Header{{1u}, static_cast<std::uint32_t>(cap - kHeaderSize), static_cast<std::uint32_t>(bytes),
                              &allocator};
    return b;
}

Buffer Buffer::copy_of(const void* data, std::size_t len) {
    Buffer b = allocate(len);
    if (b && len > 0) std::memcpy(b.data(), data, len);
    return b;
}

Buffer& Buffer::operator=(const Buffer& other) {
    if (this != &other) {
        Header* h = other.h_;
        if (h != nullptr) h->refs.fetch_add(1, std::memory_order_relaxed);
        reset();
        h_ = h;
    }
    return *this;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        reset();
        h_       = other.h_;
        other.h_ = nullptr;
    }
    return *this;
}

std::uint8_t* Buffer::data() { return h_ ? reinterpret_cast<std::uint8_t*>(h_) + kHeaderSize : nullptr; }
const std::uint8_t* Buffer::data() const {
    return h_ ? reinterpret_cast<const std::uint8_t*>(h_) + kHeaderSize : nullptr;
}

bool Buffer::resize(std::size_t n) {
    if (h_ == nullptr || n > h_->capacity) return false;
    h_->length = static_cast<std::uint32_t>(n);
    return true;
}

void Buffer::reset() {
    Header* h = h_;
    h_        = nullptr;
    if (h != nullptr && h->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        IBufferAllocator* owner = h->owner;
        h->~Header();
        owner->release(h);
    }
}

// =============================================================== BlockPool
void BlockPool::format() {
    free_ = nullptr;
    for (std::size_t i = count_; i-- > 0;) {
        auto* node = reinterpret_cast<FreeNode*>(storage_ + i * block_size_);
        node->next = free_;
        free_      = node;
    }
    used_ = 0;
}

bool BlockPool::owns(const void* p) const {
    const auto* b = static_cast<const std::uint8_t*>(p);
    return b >= storage_ && b < storage_ + block_size_ * count_;
}

void* BlockPool::take() {
    if (free_ == nullptr) return nullptr;
    FreeNode* n = free_;
    free_       = n->next;
    if (++used_ > peak_) peak_ = used_;
    return n;
}

void BlockPool::give(void* p) {
    auto* n = static_cast<FreeNode*>(p);
    n->next = free_;
    free_   = n;
    --used_;
}

// =========================================================== PoolAllocator
PoolAllocator::PoolAllocator(std::initializer_list<BlockPool*> pools) {
    for (BlockPool* p : pools) {
        if (p == nullptr || count_ == pools_.size()) continue;
        std::size_t i = count_++;
        while (i > 0 && pools_[i - 1]->block_size() > p->block_size()) {   // chèn giữ thứ tự tăng dần
            pools_[i] = pools_[i - 1];
            --i;
        }
        pools_[i] = p;
    }
}

void* PoolAllocator::allocate(std::size_t bytes, std::size_t& capacity) {
    osal::CriticalSection cs;
    BlockPool* first_fit = nullptr;
    for (std::size_t i = 0; i < count_; ++i) {
        BlockPool* p = pools_[i];
        if (p->block_size() < bytes) continue;
        if (first_fit == nullptr) first_fit = p;
        if (void* block = p->take()) {   // lớp vừa nhất hết chỗ -> thử lớp lớn hơn
            capacity = p->block_size();
            return block;
        }
    }
    if (first_fit != nullptr) first_fit->note_failure();
    return nullptr;
}

void PoolAllocator::release(void* block) {
    osal::CriticalSection cs;
    for (std::size_t i = 0; i < count_; ++i) {
        if (pools_[i]->owns(block)) {
            pools_[i]->give(block);
            return;
        }
    }
}

bool PoolAllocator::owns(const void* block) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (pools_[i]->owns(block)) return true;
    }
    return false;
}

PoolStats PoolAllocator::pool_stats(std::size_t index) const {
    osal::CriticalSection cs;
    return index < count_ ? pools_[index]->stats() : PoolStats{0, 0, 0, 0, 0};
}

// =========================================================== HeapAllocator
namespace {
constexpr std::size_t kHeapPrefix = 16;   // lưu kích thước block, giữ căn lề 16
}

void* HeapAllocator::allocate(std::size_t bytes, std::size_t& capacity) {
    {
        osal::CriticalSection cs;
        if (bytes > budget_ - used_) {
            ++failures_;
            return nullptr;
        }
        used_ += bytes;
        if (used_ > peak_) peak_ = used_;
    }
    auto* raw = static_cast<std::uint8_t*>(std::malloc(kHeapPrefix + bytes));
    if (raw == nullptr) {
        osal::CriticalSection cs;
        used_ -= bytes;
        ++failures_;
        return nullptr;
    }
    std::memcpy(raw, &bytes, sizeof(bytes));
    capacity = bytes;
    return raw + kHeapPrefix;
}

void HeapAllocator::release(void* block) {
    if (block == nullptr) return;
    auto* raw         = static_cast<std::uint8_t*>(block) - kHeapPrefix;
    std::size_t bytes = 0;
    std::memcpy(&bytes, raw, sizeof(bytes));
    std::free(raw);
    osal::CriticalSection cs;
    used_ -= bytes;
}

PoolStats HeapAllocator::pool_stats(std::size_t index) const {
    osal::CriticalSection cs;
    return index == 0 ? PoolStats{0, budget_, used_, peak_, failures_} : PoolStats{0, 0, 0, 0, 0};
}

// ========================================================== ChainAllocator
void* ChainAllocator::allocate(std::size_t bytes, std::size_t& capacity) {
    if (void* b = primary_.allocate(bytes, capacity)) return b;
    return fallback_.allocate(bytes, capacity);
}

void ChainAllocator::release(void* block) {
    if (primary_.owns(block)) {
        primary_.release(block);
    } else {
        fallback_.release(block);
    }
}

PoolStats ChainAllocator::pool_stats(std::size_t index) const {
    const std::size_t n = primary_.pool_count();
    return index < n ? primary_.pool_stats(index) : fallback_.pool_stats(index - n);
}

}  // namespace svc
