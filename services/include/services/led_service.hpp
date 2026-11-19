#pragma once
// LED chỉ báo theo "mẫu" – nghiệp vụ chỉ nói "cảnh báo", không quan tâm chân GPIO hay nhịp.
//
// Từ PR "outputs": LED là MỘT output loại Indicator (svc::Output, OutputHub quản lý vòng đời) – không còn
// thread/vòng đời riêng:
//   LedOutput  : một output, thread riêng 50 ms, lệnh = mẫu của MỌI LED (LedFrame) -> một thread cho tất cả LED
//                (tiết kiệm stack trên MCU nhỏ); tự tạo nhịp nhấp nháy; an toàn = tắt hết.
//   LedService : mặt tiền cho nghiệp vụ (set/get theo LedId), giữ khung hiện tại, gửi cả khung cho LedOutput.
// Là Indicator nên: không cần arm, failsafe (OutputHub::disarm_all) KHÔNG tắt đèn cảnh báo; tắt máy -> tắt hết.
#include <array>
#include <cstdint>

#include "hal/gpio.hpp"
#include "osal/mutex.hpp"
#include "services/output.hpp"

namespace svc {

enum class LedId : std::uint8_t { Status = 0, Alarm, Count };
enum class LedPattern : std::uint8_t { Off, On, BlinkSlow, BlinkFast, Heartbeat };

inline constexpr std::size_t kLedCount = static_cast<std::size_t>(LedId::Count);

struct LedFrame {   // lệnh của LedOutput: mẫu cho từng LED
    LedPattern pattern[kLedCount];
};

// Mức logic của một mẫu tại thời điểm now_ms (thuần, test được).
bool led_level(LedPattern pattern, std::uint64_t now_ms);

class LedOutput final : public Output<LedFrame> {
public:
    static constexpr std::uint32_t kPeriodMs = 50;
    LedOutput() : Output("leds", Config{kPeriodMs, 0, 0, nullptr, OutputClass::Indicator}) {}
    void bind(LedId id, hal::IGpio* gpio);   // trước khi OutputHub khởi động

private:
    void on_update(const LedFrame& frame, std::uint64_t now) override;
    void write_safe() override;
    struct Slot {
        hal::IGpio* gpio = nullptr;
        bool last  = false;
        bool valid = false;   // đã ghi ít nhất một lần (ghi lại chỉ khi mức đổi)
    };
    std::array<Slot, kLedCount> leds_{};
};

class LedService : core::NonCopyable {
public:
    void bind(LedId id, hal::IGpio* gpio) { out_.bind(id, gpio); }
    void set(LedId id, LedPattern pattern);   // thread-safe
    LedPattern get(LedId id) const;
    LedOutput& output() { return out_; }      // System đăng ký vào OutputHub

private:
    LedOutput out_;
    LedFrame frame_{};
    mutable osal::Mutex mtx_;
};

}  // namespace svc
