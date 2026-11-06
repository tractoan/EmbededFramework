#pragma once
// MAVLink v2 tối giản, không phụ thuộc thư viện sinh mã: đóng/mở khung, CRC (X.25 + CRC_EXTRA),
// cắt byte 0 cuối payload (v2), nhận cả khung v1, bỏ qua chữ ký (signing).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mavlink {

inline constexpr std::uint8_t kStxV1        = 0xFE;
inline constexpr std::uint8_t kStxV2        = 0xFD;
inline constexpr std::size_t kMaxPayload    = 255;
inline constexpr std::size_t kHeaderV2      = 10;   // STX..msgid
inline constexpr std::size_t kSignatureLen  = 13;
inline constexpr std::size_t kMaxFrame      = kHeaderV2 + kMaxPayload + 2 + kSignatureLen;
inline constexpr std::uint8_t kIncompatSigned = 0x01;

// CRC_EXTRA của các bản tin mà phía nhận biết (để kiểm tra CRC).
struct MessageInfo {
    std::uint32_t msgid;
    std::uint8_t crc_extra;
};

struct Message {
    std::uint32_t msgid = 0;
    std::uint8_t sysid  = 0;
    std::uint8_t compid = 0;
    std::uint8_t seq    = 0;
    std::uint8_t len    = 0;   // độ dài trên dây (trước khi mở rộng 0)
    std::uint8_t version = 2;
    std::uint8_t payload[kMaxPayload]{};   // phần sau len luôn = 0 (mở rộng theo chuẩn v2)
};

// Đóng khung v2. Trả độ dài khung (0 nếu cap không đủ). Byte 0 ở cuối payload bị cắt (giữ tối thiểu 1 byte).
std::size_t encode(std::uint8_t* out, std::size_t cap, std::uint8_t seq, std::uint8_t sysid, std::uint8_t compid,
                   std::uint32_t msgid, std::uint8_t crc_extra, const std::uint8_t* payload, std::size_t len);

// Tra CRC_EXTRA theo msgid. Trả false nếu không biết bản tin -> không kiểm được CRC -> bỏ khung.
using CrcLookup = bool (*)(std::uint32_t msgid, std::uint8_t& crc_extra, const void* ctx);

// Bộ tách khung (v1 + v2) trên bộ đệm: khung sai header/CRC chỉ bỏ 1 byte rồi quét lại, nên
// một byte STX giả trong rác không làm mất khung hợp lệ phía sau.
class Parser {
public:
    Parser(CrcLookup lookup, const void* ctx) : lookup_(lookup), ctx_(ctx) {}

    // Thêm một byte. true khi có bản tin hoàn chỉnh (đọc bằng message()); sau đó gọi next()
    // cho tới khi false để lấy các bản tin còn lại trong bộ đệm (hiếm, sau khi tái đồng bộ).
    bool push(std::uint8_t b);
    bool next();
    const Message& message() const { return msg_; }
    void reset() { len_ = 0; }

    // Tiện ích: đưa cả khối, gọi on_msg(const Message&) cho từng bản tin.
    template <typename F>
    void feed(const std::uint8_t* data, std::size_t n, F&& on_msg) {
        for (std::size_t i = 0; i < n; ++i) {
            if (!push(data[i])) continue;
            do {
                on_msg(msg_);
            } while (next());
        }
    }

    std::uint32_t crc_errors() const { return crc_errors_; }
    std::uint32_t unknown() const { return unknown_; }
    std::size_t buffered() const { return len_; }

private:
    void drop(std::size_t n);

    CrcLookup lookup_;
    const void* ctx_;
    std::uint8_t buf_[kMaxFrame]{};
    std::size_t len_ = 0;
    Message msg_{};
    std::uint32_t crc_errors_ = 0, unknown_ = 0;
};

// Kiểm tra data có bắt đầu bằng một khung hoàn chỉnh, đúng CRC hay không (dùng cho auto-detect).
bool starts_with_valid_frame(const std::uint8_t* data, std::size_t len, CrcLookup lookup, const void* ctx);

// ---------------------------------------------------- đọc/ghi little-endian
namespace wire {
inline void put_u8(std::uint8_t* p, std::size_t off, std::uint8_t v) { p[off] = v; }
inline void put_u16(std::uint8_t* p, std::size_t off, std::uint16_t v) {
    p[off]     = static_cast<std::uint8_t>(v);
    p[off + 1] = static_cast<std::uint8_t>(v >> 8);
}
inline void put_u32(std::uint8_t* p, std::size_t off, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) p[off + k] = static_cast<std::uint8_t>(v >> (8 * k));
}
inline void put_i32(std::uint8_t* p, std::size_t off, std::int32_t v) { put_u32(p, off, static_cast<std::uint32_t>(v)); }
inline void put_f32(std::uint8_t* p, std::size_t off, float v) {
    std::uint32_t u;
    std::memcpy(&u, &v, 4);
    put_u32(p, off, u);
}
inline std::uint16_t get_u16(const std::uint8_t* p, std::size_t off) {
    return static_cast<std::uint16_t>(p[off] | (p[off + 1] << 8));
}
inline std::uint32_t get_u32(const std::uint8_t* p, std::size_t off) {
    return static_cast<std::uint32_t>(p[off]) | (static_cast<std::uint32_t>(p[off + 1]) << 8) |
           (static_cast<std::uint32_t>(p[off + 2]) << 16) | (static_cast<std::uint32_t>(p[off + 3]) << 24);
}
inline float get_f32(const std::uint8_t* p, std::size_t off) {
    const std::uint32_t u = get_u32(p, off);
    float v;
    std::memcpy(&v, &u, 4);
    return v;
}
}  // namespace wire

}  // namespace mavlink
