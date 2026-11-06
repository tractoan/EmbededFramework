#include "services/led_service.hpp"

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace svc {

void LedService::bind(LedId id, hal::IGpio* gpio) {
    const auto i = static_cast<std::size_t>(id);
    if (i < leds_.size()) leds_[i].gpio = gpio;
}

void LedService::set(LedId id, LedPattern pattern) {
    const auto i = static_cast<std::size_t>(id);
    if (i >= leds_.size()) return;
    static constexpr const char* kNames[] = {"Off", "On", "BlinkSlow", "BlinkFast", "Heartbeat"};
    if (leds_[i].pattern.exchange(pattern) != pattern) {
        LOGD("led", "led%u -> %s", static_cast<unsigned>(i), kNames[static_cast<std::size_t>(pattern)]);
    }
}

LedPattern LedService::get(LedId id) const {
    const auto i = static_cast<std::size_t>(id);
    return i < leds_.size() ? leds_[i].pattern.load() : LedPattern::Off;
}

void LedService::tick(std::uint64_t now) {
    for (auto& led : leds_) {
        if (led.gpio == nullptr) continue;
        bool level = false;
        switch (led.pattern.load()) {
            case LedPattern::Off:       level = false; break;
            case LedPattern::On:        level = true; break;
            case LedPattern::BlinkSlow: level = (now / 500) % 2 != 0; break;
            case LedPattern::BlinkFast: level = (now / 125) % 2 != 0; break;
            case LedPattern::Heartbeat: level = (now % 1000) < 80; break;
        }
        if (!led.valid || level != led.last) {
            led.gpio->write(level);
            led.last  = level;
            led.valid = true;
        }
    }
}

core::Status LedService::start(const osal::ThreadConfig& task) {
    running_ = true;
    return thread_.start(task,
                         &osal::Thread::invoke<LedService, &LedService::run>, this);
}

void LedService::stop() {
    running_ = false;
    thread_.join();
}

void LedService::run() {
    while (running_) {
        tick(osal::now_ms());
        osal::sleep_ms(50);
    }
}

}  // namespace svc
