#include "device/tmp102.hpp"

namespace device {

namespace {
constexpr std::uint8_t kRegTemp   = 0x00;
constexpr std::uint8_t kRegConfig = 0x01;
// Byte 1: OS R1 R0 F1 F0 POL TM SD    Byte 2: CR1 CR0 AL EM 0 0 0 0
constexpr std::uint8_t kSd = 0x01;
constexpr std::uint8_t kEm = 0x10;
constexpr std::uint8_t kCrShift = 6;
}  // namespace

core::Status Tmp102::read_deci_celsius(std::int32_t& out) {
    if (bus_ == nullptr) return core::Status::NotSupported;
    std::uint8_t rx[2] = {};
    const core::Status st = bus_->write_read(addr_, &kRegTemp, 1, rx, sizeof(rx), 20);
    if (!core::is_ok(st)) return st;

    // Bù 2, căn trái: 12 bit (>> 4) hoặc 13 bit ở chế độ mở rộng (>> 3); 1 LSB = 0.0625 °C = 1/16 °C
    const auto word = static_cast<std::int16_t>(static_cast<std::uint16_t>((rx[0] << 8) | rx[1]));
    const std::int32_t raw = extended_.load() ? (word >> 3) : (word >> 4);
    const std::int32_t x10 = raw * 10;
    out = (x10 + (x10 >= 0 ? 8 : -8)) / 16;   // làm tròn về 0.1 °C
    return core::Status::Ok;
}

core::Status Tmp102::apply(const Tmp102Config& cfg) {
    if (bus_ == nullptr) return core::Status::NotSupported;
    std::uint8_t cur[2] = {};
    core::Status st = bus_->write_read(addr_, &kRegConfig, 1, cur, sizeof(cur), 20);
    if (!core::is_ok(st)) return st;
    std::uint8_t b1 = static_cast<std::uint8_t>(cfg.shutdown ? (cur[0] | kSd) : (cur[0] & ~kSd));
    std::uint8_t b2 = static_cast<std::uint8_t>((cur[1] & ~(0xC0u | kEm)) |
                                                (static_cast<std::uint8_t>(cfg.rate) << kCrShift) |
                                                (cfg.extended ? kEm : 0u));
    const std::uint8_t tx[3] = {kRegConfig, b1, b2};
    st = bus_->write(addr_, tx, sizeof(tx), 20);
    if (core::is_ok(st)) extended_.store(cfg.extended);   // chỉ đổi cách giải mã khi chip đã nhận cấu hình
    return st;
}

}  // namespace device
