#pragma once
#include <array>
#include <atomic>

#include "hal/gpio.hpp"
#include "osal/thread.hpp"

namespace svc {

enum class LedId : std::uint8_t { Status = 0, Alarm, Count };
enum class LedPattern : std::uint8_t { Off, On, BlinkSlow, BlinkFast, Heartbeat };

// Điều khiển LED theo "pattern" – nghiệp vụ chỉ nói "cảnh báo", không quan tâm chân GPIO hay timing.
class LedService : core::NonCopyable {
public:
    void bind(LedId id, hal::IGpio* gpio);
    void set(LedId id, LedPattern pattern);
    LedPattern get(LedId id) const;

    core::Status start();   // thread riêng, chu kỳ 50 ms
    void stop();
    void tick(std::uint64_t now_ms);   // public: có thể gọi từ super-loop (bare-metal) hoặc test

private:
    void run();
    struct Slot {
        hal::IGpio* gpio = nullptr;
        std::atomic<LedPattern> pattern{LedPattern::Off};
        bool last  = false;
        bool valid = false;
    };
    std::array<Slot, static_cast<std::size_t>(LedId::Count)> leds_;
    osal::Thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace svc
