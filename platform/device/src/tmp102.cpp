#include "device/tmp102.hpp"

namespace device {

core::Status Tmp102::read_deci_celsius(std::int32_t& out) {
    if (bus_ == nullptr) return core::Status::NotSupported;
    const std::uint8_t reg = 0x00;   // Temperature register
    std::uint8_t rx[2] = {};
    const core::Status st = bus_->write_read(addr_, &reg, 1, rx, sizeof(rx), 20);
    if (!core::is_ok(st)) return st;

    // 12-bit bù 2, căn trái; 1 LSB = 0.0625 °C = 1/16 °C
    const auto raw = static_cast<std::int16_t>(static_cast<std::uint16_t>((rx[0] << 8) | rx[1])) >> 4;
    const std::int32_t x10 = raw * 10;
    out = (x10 + (x10 >= 0 ? 8 : -8)) / 16;   // làm tròn về 0.1 °C
    return core::Status::Ok;
}

}  // namespace device
