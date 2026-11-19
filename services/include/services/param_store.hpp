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
// Sau mỗi lần load() (thành công hay không – giá trị mặc định cũng là "đã sẵn sàng"): ai cấu hình theo tham số
// (vd. SensorParamBinding) áp dụng giá trị hiện tại. load() đặt giá trị trực tiếp, không phát kParamChanged.
inline constexpr msg::EventId kParamsLoaded = msg::evt::kServiceBase + 6;   // payload: svc::ParamsLoaded
}  // namespace evt

struct ParamChanged {   // payload của evt::kParamChanged
    ParamId id;
    std::int32_t value;
};

struct ParamsLoaded {   // payload của evt::kParamsLoaded
    core::Status result;   // Ok: đã nạp từ NV; khác: đang dùng mặc định
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

    core::Status load();   // đọc từ NV (giá trị ngoài miền bị bỏ qua); luôn phát evt::kParamsLoaded
    core::Status save();   // erase + ghi lại toàn bộ

private:
    struct Entry {
        const ParamDef* def = nullptr;
        std::int32_t value  = 0;
    };
    int index_of(ParamId id) const;   // gọi khi đang giữ khóa
    core::Status load_from_nv();

    std::array<Entry, core::cfg::kMaxParams> entries_{};
    std::size_t count_          = 0;
    hal::INvStorage* storage_   = nullptr;
    msg::EventBus* bus_         = nullptr;
    mutable osal::Mutex mtx_;   // bảo vệ entries_/count_
    osal::Mutex nv_mtx_;        // tuần tự hóa save/load: CLI và Modbus có thể cùng lưu (thứ tự khóa: nv_mtx_ -> mtx_)
};

}  // namespace svc
