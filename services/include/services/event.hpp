#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "core/config.hpp"
#include "services/buffer.hpp"

namespace svc {

using EventId = std::uint16_t;

// Phân vùng EventId theo lớp để tránh trùng lặp giữa các nhóm phát triển.
namespace evt {
inline constexpr EventId kAny         = 0xFFFF;   // wildcard khi subscribe
inline constexpr EventId kSystemBase  = 0x0000;
inline constexpr EventId kServiceBase = 0x0100;
inline constexpr EventId kAppBase     = 0x1000;   // nghiệp vụ: 0x1000..0xFFFE

inline constexpr EventId kSystemStarted  = kSystemBase + 1;
inline constexpr EventId kSystemShutdown = kSystemBase + 2;
inline constexpr EventId kParamChanged   = kServiceBase + 1;   // payload: svc::ParamChanged
}  // namespace evt

// Event = message đi qua EventBus / queue, copy theo giá trị -> an toàn giữa các thread.
// Hai phần dữ liệu, dùng riêng hoặc kết hợp:
//   payload : nhỏ (<= EP_CFG_EVENT_PAYLOAD_SIZE), nằm ngay trong Event, không cấp phát.
//   buffer  : lớn / kích thước thay đổi, svc::Buffer đếm tham chiếu – copy Event chỉ tăng
//             tham chiếu, mọi listener dùng chung một vùng nhớ, tự giải phóng khi hết người dùng.
// Mẫu thường gặp: payload chứa "header" mô tả (số mẫu, định dạng...), buffer chứa dữ liệu.
struct Event {
    EventId id                 = 0;
    std::uint16_t size         = 0;   // kích thước payload inline
    std::uint32_t timestamp_ms = 0;
    alignas(8) std::uint8_t payload[core::cfg::kEventPayloadSize]{};
    Buffer buffer;                    // payload lớn (tùy chọn)

    static Event make(EventId id) {
        Event e;
        e.id = id;
        return e;
    }

    template <typename T>
    static Event make(EventId id, const T& data) {
        static_assert(std::is_trivially_copyable<T>::value, "Event payload must be trivially copyable");
        static_assert(sizeof(T) <= core::cfg::kEventPayloadSize, "Payload too large: raise EP_CFG_EVENT_PAYLOAD_SIZE");
        Event e;
        e.id   = id;
        e.size = static_cast<std::uint16_t>(sizeof(T));
        std::memcpy(e.payload, &data, sizeof(T));
        return e;
    }

    // Payload lớn (không có header inline)
    static Event make(EventId id, Buffer data) {
        Event e;
        e.id     = id;
        e.buffer = std::move(data);
        return e;
    }

    // Header nhỏ inline + payload lớn
    template <typename T>
    static Event make(EventId id, const T& header, Buffer data) {
        Event e  = make(id, header);
        e.buffer = std::move(data);
        return e;
    }

    template <typename T>
    bool get(T& out) const {
        static_assert(std::is_trivially_copyable<T>::value, "Event payload must be trivially copyable");
        if (size != sizeof(T)) return false;
        std::memcpy(&out, payload, sizeof(T));
        return true;
    }
};

class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void on_event(const Event& e) = 0;
};

}  // namespace svc
