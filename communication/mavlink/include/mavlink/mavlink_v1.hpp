#pragma once
// Codec MAVLink v1 tối giản (không cấp phát, không phụ thuộc thư viện sinh mã).
//
// Khung v1:  STX(0xFE) LEN SEQ SYSID COMPID MSGID PAYLOAD[LEN] CRC_LO CRC_HI
// CRC = CRC-16/MCRF4XX trên LEN..PAYLOAD, rồi cộng thêm CRC_EXTRA của message (băm định nghĩa).
// Chỉ các message trong bảng kMessages được nhận/gửi; message lạ bị bỏ (không kiểm được CRC).
// Độ dài & CRC_EXTRA lấy từ định nghĩa common.xml (đối chiếu với pymavlink dialects/v10/common.py).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mavlink {

inline constexpr std::uint8_t kStx            = 0xFE;
inline constexpr std::size_t kHeaderLen       = 6;    // STX..MSGID
inline constexpr std::size_t kMaxPayload      = 255;
inline constexpr std::size_t kMaxFrame        = kHeaderLen + kMaxPayload + 2;

namespace msgid {
inline constexpr std::uint8_t kHeartbeat        = 0;
inline constexpr std::uint8_t kParamRequestRead = 20;
inline constexpr std::uint8_t kParamRequestList = 21;
inline constexpr std::uint8_t kParamValue       = 22;
inline constexpr std::uint8_t kParamSet         = 23;
inline constexpr std::uint8_t kCommandLong      = 76;
inline constexpr std::uint8_t kCommandAck       = 77;
inline constexpr std::uint8_t kNamedValueFloat  = 251;
inline constexpr std::uint8_t kStatustext       = 253;
}  // namespace msgid

// Một số giá trị enum của common.xml dùng trong framework
namespace mav {
inline constexpr std::uint8_t kTypeOnboardController = 18;   // MAV_TYPE_ONBOARD_CONTROLLER
inline constexpr std::uint8_t kAutopilotInvalid      = 8;    // MAV_AUTOPILOT_INVALID
inline constexpr std::uint8_t kStateStandby          = 3;    // MAV_STATE_*
inline constexpr std::uint8_t kStateActive           = 4;
inline constexpr std::uint8_t kStateCritical         = 5;
inline constexpr std::uint8_t kParamTypeReal32       = 9;    // MAV_PARAM_TYPE_REAL32
inline constexpr std::uint8_t kResultAccepted        = 0;    // MAV_RESULT_*
inline constexpr std::uint8_t kResultDenied          = 2;
inline constexpr std::uint8_t kResultUnsupported     = 3;
inline constexpr std::uint8_t kResultFailed          = 4;
inline constexpr std::uint8_t kSeverityCritical      = 2;    // MAV_SEVERITY_*
inline constexpr std::uint8_t kSeverityWarning       = 4;
inline constexpr std::uint8_t kSeverityInfo          = 6;
inline constexpr std::uint16_t kCmdPreflightStorage  = 245;    // MAV_CMD_PREFLIGHT_STORAGE
inline constexpr std::uint16_t kCmdUser1             = 31010;  // MAV_CMD_USER_1
inline constexpr std::uint8_t kMavlinkVersion        = 3;
}  // namespace mav

struct MessageInfo {
    std::uint8_t id;
    std::uint8_t length;      // độ dài payload (v1: cố định)
    std::uint8_t crc_extra;
};
const MessageInfo* find_message(std::uint8_t id);

// Khung đã giải mã; payload trỏ vào bộ đệm của Parser, chỉ hợp lệ trong callback.
struct Frame {
    std::uint8_t seq;
    std::uint8_t sysid;
    std::uint8_t compid;
    std::uint8_t msgid;
    std::uint8_t len;
    const std::uint8_t* payload;
};

// Đóng khung; out cần kMaxFrame byte. Trả độ dài khung, 0 nếu msgid lạ hoặc len sai.
std::size_t encode(std::uint8_t* out, std::uint8_t seq, std::uint8_t sysid, std::uint8_t compid, std::uint8_t msgid,
                   const std::uint8_t* payload, std::size_t len);

// Khung hoàn chỉnh, hợp lệ (msgid biết, độ dài đúng, CRC đúng) nằm ở đầu data?
bool is_valid_frame(const std::uint8_t* data, std::size_t len, std::size_t* frame_len = nullptr);

// Tách khung từ luồng byte: chịu được rác, khung bị chia nhỏ, CRC sai (tự tái đồng bộ).
class Parser {
public:
    using Callback = void (*)(void* ctx, const Frame& frame);

    void feed(const std::uint8_t* data, std::size_t len, Callback cb, void* ctx);
    void reset() { n_ = 0; }

    std::uint32_t frames_ok() const { return frames_ok_; }
    std::uint32_t rejected() const { return rejected_; }   // khung sai CRC / msgid lạ / độ dài sai

private:
    void consume(std::size_t k);
    std::uint8_t buf_[2 * kMaxFrame]{};
    std::size_t n_          = 0;
    std::uint32_t frames_ok_ = 0;
    std::uint32_t rejected_  = 0;
};

// ---- đọc/ghi little-endian (đúng trên mọi CPU, không phụ thuộc căn lề) ----
inline void put_u16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
inline void put_u32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void put_f32(std::uint8_t* p, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, sizeof(v));
    put_u32(p, v);
}
inline std::uint16_t get_u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
inline std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
inline float get_f32(const std::uint8_t* p) {
    const std::uint32_t v = get_u32(p);
    float f;
    std::memcpy(&f, &v, sizeof(f));
    return f;
}
// Chép chuỗi vào trường char[N] của MAVLink: đệm 0, KHÔNG bắt buộc kết thúc '\0' khi đủ N ký tự.
inline void put_str(std::uint8_t* p, std::size_t n, const char* s) {
    std::size_t i = 0;
    for (; s != nullptr && s[i] != '\0' && i < n; ++i) p[i] = static_cast<std::uint8_t>(s[i]);
    for (; i < n; ++i) p[i] = 0;
}
// So trường char[N] (có thể không có '\0') với chuỗi C.
inline bool str_equals(const std::uint8_t* p, std::size_t n, const char* s) {
    std::size_t i = 0;
    for (; i < n && p[i] != 0; ++i) {
        if (s[i] == '\0' || s[i] != static_cast<char>(p[i])) return false;
    }
    return i == n ? true : s[i] == '\0';
}

}  // namespace mavlink
