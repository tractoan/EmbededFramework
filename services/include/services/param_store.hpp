#pragma once
#include <array>
#include <cstdint>

#include "hal/storage.hpp"
#include "messaging/event_bus.hpp"
#include "osal/mutex.hpp"

namespace svc {

using ParamId = std::uint16_t;

// Định nghĩa tham số do module nghiệp vụ khai báo (đối tượng phải sống suốt chương trình).
struct ParamDef {
    ParamId id;
    const char* name;
    std::int32_t default_value;
    std::int32_t min;
    std::int32_t max;
    const char* unit;
};

// Event do services phát (dải msg::evt::kServiceBase).
namespace evt {
inline constexpr msg::EventId kParamChanged = msg::evt::kServiceBase + 1;   // payload: svc::ParamChanged
}  // namespace evt

struct ParamChanged {   // payload của evt::kParamChanged
    ParamId id;
    std::int32_t value;
};

// Kho tham số cấu hình runtime: kiểm tra miền giá trị, lưu NV có CRC,
// phát evt::kParamChanged khi thay đổi -> nghiệp vụ tùy biến lúc chạy không cần build lại.
class ParamStore : core::NonCopyable {
public:
    void init(hal::INvStorage* storage, msg::EventBus* bus);

    core::Status register_params(const ParamDef* defs, std::size_t count);

    core::Status get(ParamId id, std::int32_t& out) const;
    std::int32_t get_or(ParamId id, std::int32_t fallback) const;
    core::Status set(ParamId id, std::int32_t value);
    core::Status reset_defaults();

    const ParamDef* find(const char* name) const;
    std::size_t count() const;
    const ParamDef* at(std::size_t index, std::int32_t* value = nullptr) const;

    core::Status load();   // đọc từ NV (giá trị ngoài miền bị bỏ qua)
    core::Status save();   // erase + ghi lại toàn bộ

private:
    struct Entry {
        const ParamDef* def = nullptr;
        std::int32_t value  = 0;
    };
    int index_of(ParamId id) const;   // gọi khi đang giữ khóa

    std::array<Entry, core::cfg::kMaxParams> entries_{};
    std::size_t count_          = 0;
    hal::INvStorage* storage_   = nullptr;
    msg::EventBus* bus_         = nullptr;
    mutable osal::Mutex mtx_;
};

}  // namespace svc
