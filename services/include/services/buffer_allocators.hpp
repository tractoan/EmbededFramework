#pragma once
// Các chiến lược cấp phát cho svc::Buffer.
//
//   PoolAllocator  – nhiều lớp kích thước cố định, bộ nhớ tĩnh: O(1), không phân mảnh,
//                    tất định -> khuyến nghị cho MCU. Lớp vừa nhất hết chỗ -> dùng lớp lớn hơn.
//   HeapAllocator  – malloc/free có giới hạn tổng byte: cho payload rất lớn / kích thước khó đoán.
//   ChainAllocator – thử allocator chính trước, hết thì dùng allocator dự phòng (vd. pool -> heap).
//
//   static svc::StaticBlockPool<64, 16>   small;
//   static svc::StaticBlockPool<512, 8>   medium;
//   static svc::StaticBlockPool<2048, 4>  large;
//   static svc::PoolAllocator pools({&small, &medium, &large});
//   svc::Buffer::set_default_allocator(&pools);
#include <array>
#include <cstddef>
#include <initializer_list>

#include "core/config.hpp"
#include "core/noncopyable.hpp"
#include "services/buffer.hpp"

#ifndef EP_CFG_MAX_BUFFER_POOLS
#define EP_CFG_MAX_BUFFER_POOLS 8
#endif

namespace svc {

// Một pool các block cùng kích thước, danh sách rỗi liên kết ngay trong block trống.
// KHÔNG tự khóa – được PoolAllocator bảo vệ.
class BlockPool : core::NonCopyable {
public:
    std::size_t block_size() const { return block_size_; }
    std::size_t count() const { return count_; }
    bool owns(const void* p) const;

    void* take();
    void give(void* p);
    PoolStats stats() const { return PoolStats{block_size_, count_, used_, peak_, failures_}; }
    void note_failure() { ++failures_; }

protected:
    BlockPool(std::uint8_t* storage, std::size_t block_size, std::size_t count)
        : storage_(storage), block_size_(block_size), count_(count) {}
    void format();   // xây danh sách rỗi – gọi từ constructor lớp con khi storage đã sẵn sàng

private:
    struct FreeNode {
        FreeNode* next;
    };
    std::uint8_t* storage_;
    std::size_t block_size_;
    std::size_t count_;
    FreeNode* free_      = nullptr;
    std::size_t used_    = 0;
    std::size_t peak_    = 0;
    std::uint32_t failures_ = 0;
};

template <std::size_t BlockSize, std::size_t Count>
class StaticBlockPool final : public BlockPool {
    static_assert(BlockSize >= sizeof(void*) && BlockSize % 8 == 0, "BlockSize must be a multiple of 8");
    static_assert(Count > 0, "Count must be > 0");

public:
    StaticBlockPool() : BlockPool(storage_, BlockSize, Count) { format(); }

private:
    alignas(8) std::uint8_t storage_[BlockSize * Count];
};

class PoolAllocator final : public IBufferAllocator, private core::NonCopyable {
public:
    explicit PoolAllocator(std::initializer_list<BlockPool*> pools);   // tự sắp xếp theo kích thước

    void* allocate(std::size_t bytes, std::size_t& capacity) override;
    void release(void* block) override;
    bool owns(const void* block) const override;
    std::size_t pool_count() const override { return count_; }
    PoolStats pool_stats(std::size_t index) const override;

private:
    std::array<BlockPool*, EP_CFG_MAX_BUFFER_POOLS> pools_{};
    std::size_t count_ = 0;
};

class HeapAllocator final : public IBufferAllocator, private core::NonCopyable {
public:
    explicit HeapAllocator(std::size_t budget_bytes) : budget_(budget_bytes) {}

    void* allocate(std::size_t bytes, std::size_t& capacity) override;
    void release(void* block) override;
    bool owns(const void* block) const override { return block != nullptr; }
    std::size_t pool_count() const override { return 1; }
    PoolStats pool_stats(std::size_t index) const override;

private:
    std::size_t budget_;
    std::size_t used_       = 0;
    std::size_t peak_       = 0;
    std::uint32_t failures_ = 0;
};

class ChainAllocator final : public IBufferAllocator, private core::NonCopyable {
public:
    ChainAllocator(IBufferAllocator& primary, IBufferAllocator& fallback) : primary_(primary), fallback_(fallback) {}

    void* allocate(std::size_t bytes, std::size_t& capacity) override;
    void release(void* block) override;
    bool owns(const void* block) const override { return primary_.owns(block) || fallback_.owns(block); }
    std::size_t pool_count() const override { return primary_.pool_count() + fallback_.pool_count(); }
    PoolStats pool_stats(std::size_t index) const override;

private:
    IBufferAllocator& primary_;
    IBufferAllocator& fallback_;
};

}  // namespace svc
