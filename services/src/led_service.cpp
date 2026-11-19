#include "services/led_service.hpp"

#include "services/logger.hpp"

namespace svc {

bool led_level(LedPattern pattern, std::uint64_t now) {
    switch (pattern) {
        case LedPattern::Off:       return false;
        case LedPattern::On:        return true;
        case LedPattern::BlinkSlow: return (now / 500) % 2 != 0;
        case LedPattern::BlinkFast: return (now / 125) % 2 != 0;
        case LedPattern::Heartbeat: return (now % 1000) < 80;
    }
    return false;
}

void LedOutput::bind(LedId id, hal::IGpio* gpio) {
    const auto i = static_cast<std::size_t>(id);
    if (i < leds_.size()) leds_[i].gpio = gpio;
}

void LedOutput::on_update(const LedFrame& frame, std::uint64_t now) {
    for (std::size_t i = 0; i < leds_.size(); ++i) {
        Slot& led = leds_[i];
        if (led.gpio == nullptr) continue;
        const bool level = led_level(frame.pattern[i], now);
        if (!led.valid || level != led.last) {
            led.gpio->write(level);
            led.last  = level;
            led.valid = true;
        }
    }
}

void LedOutput::write_safe() {
    for (Slot& led : leds_) {
        if (led.gpio == nullptr) continue;
        if (!led.valid || led.last) led.gpio->write(false);
        led.last  = false;
        led.valid = true;
    }
}

void LedService::set(LedId id, LedPattern pattern) {
    const auto i = static_cast<std::size_t>(id);
    if (i >= kLedCount) return;
    static constexpr const char* kNames[] = {"Off", "On", "BlinkSlow", "BlinkFast", "Heartbeat"};
    bool changed;
    {
        osal::LockGuard g(mtx_);   // khung chung của mọi LED: hai nơi set hai LED khác nhau không ghi đè nhau
        changed           = frame_.pattern[i] != pattern;
        frame_.pattern[i] = pattern;
        out_.command(frame_);      // Busy khi hệ thống đang tắt: bỏ qua
    }
    if (changed) LOGD("led", "led%u -> %s", static_cast<unsigned>(i), kNames[static_cast<std::size_t>(pattern)]);
}

LedPattern LedService::get(LedId id) const {
    const auto i = static_cast<std::size_t>(id);
    if (i >= kLedCount) return LedPattern::Off;
    osal::LockGuard g(mtx_);
    return frame_.pattern[i];
}

}  // namespace svc
