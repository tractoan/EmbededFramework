#pragma once
// Bảng ánh xạ Modbus <-> mô hình dữ liệu chuẩn của hệ thống (Event, Param, hành động).
// Do SẢN PHẨM khai báo -> giao thức không biết gì về nghiệp vụ, nghiệp vụ không biết Modbus.
#include <cstddef>
#include <cstdint>

#include "comm/protocol.hpp"

namespace modbus {

// Input register (FC04, chỉ đọc): giá trị mới nhất trích từ một Event miền.
struct InputRegister {
    std::uint16_t address;
    msg::EventId event;
    bool (*extract)(const msg::Event& e, std::int32_t& value);
    bool is_signed;
};

// Holding register (FC03 đọc, FC06/FC16 ghi): ánh xạ trực tiếp tới tham số ParamStore
// (kiểm tra miền giá trị, phát kParamChanged, lưu Flash được – không cần code thêm).
struct HoldingRegister {
    std::uint16_t address;
    svc::ParamId param;
    bool is_signed;
};

// Coil (FC05 ghi): một hành động/lệnh, vd. xác nhận cảnh báo, lưu cấu hình.
struct Coil {
    std::uint16_t address;
    core::Status (*action)(comm::Services& services, bool on);
};

struct Map {
    std::uint8_t unit_id;
    const InputRegister* inputs;
    std::size_t input_count;
    const HoldingRegister* holdings;
    std::size_t holding_count;
    const Coil* coils;
    std::size_t coil_count;
};

template <std::size_t NI, std::size_t NH, std::size_t NC>
constexpr Map make_map(std::uint8_t unit_id, const InputRegister (&in)[NI], const HoldingRegister (&hold)[NH],
                       const Coil (&coils)[NC]) {
    return Map{unit_id, in, NI, hold, NH, coils, NC};
}

}  // namespace modbus
