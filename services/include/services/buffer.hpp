#pragma once
// Bộ đệm payload kích thước tùy ý, đếm tham chiếu, cấp phát qua allocator thay được.
//
//   svc::Buffer buf = svc::Buffer::allocate(1500);       // lấy từ allocator mặc định
//   std::memcpy(buf.data(), frame, 1500);                 // GHI trước khi publish
//   bus.publish(svc::Event::make(kFrame, FrameInfo{...}, std::move(buf)));
//   ...
//   void on_event(const svc::Event& e) { const svc::Buffer& b = e.buffer; use(b.data(), b.size()); }
//
// Quy ước: chủ sở hữu ghi dữ liệu TRƯỚC khi chia sẻ; sau khi publish, buffer coi như chỉ đọc
// (mọi listener cùng trỏ tới một vùng nhớ, không copy). Listener muốn giữ lâu hơn on_event()
// thì copy Buffer (tăng tham chiếu, không copy dữ liệu). Vùng nhớ tự trả về allocator khi
// tham chiếu cuối cùng bị hủy.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace svc {

// Thống kê một "lớp" của allocator (một pool kích thước cố định).
struct PoolStats {
    std::size_t block_size;
    std::size_t capacity;     // số block
    std::size_t used;         // số block đang dùng
    std::size_t peak;         // mức dùng cao nhất từng đạt
    std::uint32_t failures;   // số lần cấp phát thất bại
};

// Chiến lược cấp phát – thay được theo sản phẩm (các pool tĩnh, kết hợp nhiều pool...).
// Phải thread-safe. Chưa hỗ trợ gọi từ ISR.
class IBufferAllocator {
public:
    virtual ~IBufferAllocator() = default;
    // Cấp một block >= bytes; capacity = kích thước thực của block. nullptr nếu hết.
    virtual void* allocate(std::size_t bytes, std::size_t& capacity) = 0;
    virtual void release(void* block)                                 = 0;
    virtual bool owns(const void* block) const                        = 0;
    virtual std::size_t pool_count() const                            = 0;
    virtual PoolStats pool_stats(std::size_t index) const             = 0;
};

class Buffer {
public:
    Buffer() = default;
    ~Buffer() { reset(); }
    Buffer(const Buffer& other) : h_(other.h_) { retain(); }
    Buffer(Buffer&& other) noexcept : h_(other.h_) { other.h_ = nullptr; }
    Buffer& operator=(const Buffer& other);
    Buffer& operator=(Buffer&& other) noexcept;

    // Cấp `bytes` byte từ allocator mặc định / allocator chỉ định. Rỗng nếu thất bại.
    static Buffer allocate(std::size_t bytes);
    static Buffer allocate(IBufferAllocator& allocator, std::size_t bytes);
    static Buffer copy_of(const void* data, std::size_t len);

    static void set_default_allocator(IBufferAllocator* allocator);
    static IBufferAllocator* default_allocator();

    explicit operator bool() const { return h_ != nullptr; }
    std::uint8_t* data();
    const std::uint8_t* data() const;
    std::size_t size() const { return h_ ? h_->length : 0; }       // số byte hợp lệ
    std::size_t capacity() const { return h_ ? h_->capacity : 0; } // tối đa có thể resize tới
    bool resize(std::size_t n);                                    // n <= capacity()
    std::uint32_t use_count() const { return h_ ? h_->refs.load() : 0; }
    void reset();

private:
    struct alignas(8) Header {
        std::atomic<std::uint32_t> refs;
        std::uint32_t capacity;
        std::uint32_t length;
        IBufferAllocator* owner;
    };
    static constexpr std::size_t kHeaderSize = (sizeof(Header) + 7u) & ~std::size_t{7u};

    void retain() {
        if (h_ != nullptr) h_->refs.fetch_add(1, std::memory_order_relaxed);
    }

    Header* h_ = nullptr;
};

}  // namespace svc
