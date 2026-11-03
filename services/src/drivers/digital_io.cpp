#include "services/drivers/digital_io.hpp"

namespace svc {

core::Status GpioInputBank::add(hal::IGpio* pin) {
    if (pin == nullptr) return core::Status::InvalidArg;
    if (count_ >= pins_.size()) return core::Status::NoMemory;
    pins_[count_++] = pin;
    return core::Status::Ok;
}

std::uint32_t GpioInputBank::read() {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        if (pins_[i]->read()) v |= 1u << i;
    }
    return v ^ (invert_ & (count_ >= 32 ? 0xFFFFFFFFu : ((1u << count_) - 1u)));
}

core::Status GpioOutputBank::add(hal::IGpio* pin) {
    if (pin == nullptr) return core::Status::InvalidArg;
    if (count_ >= pins_.size()) return core::Status::NoMemory;
    pins_[count_++] = pin;
    return pin->write(false);
}

core::Status GpioOutputBank::write(std::uint32_t mask, std::uint32_t value) {
    const std::uint32_t valid = count_ >= 32 ? 0xFFFFFFFFu : ((1u << count_) - 1u);
    if ((mask & ~valid) != 0) return core::Status::OutOfRange;
    osal::LockGuard g(mtx_);
    const std::uint32_t next = (state_.load() & ~mask) | (value & mask);
    core::Status result = core::Status::Ok;
    for (std::size_t i = 0; i < count_; ++i) {
        if (((mask >> i) & 1u) == 0) continue;
        const core::Status st = pins_[i]->write(((next >> i) & 1u) != 0);
        if (!core::is_ok(st)) result = st;
    }
    state_ = next;
    return result;
}

}  // namespace svc
