#pragma once
// Lịch định kỳ KHÔNG trôi, dùng chung cho SensorScheduler và Actuator (thread riêng):
//  - mốc kế tiếp = mốc trước + chu kỳ (thời gian chạy không cộng dồn vào chu kỳ);
//  - trễ hơn một chu kỳ trọn vẹn -> đếm overrun, đặt lại lịch từ bây giờ, KHÔNG chạy dồn các lần lỡ.
#include <cstdint>

namespace svc {

class PeriodicSchedule {
public:
    // Đến hạn chưa? Lần gọi đầu tiên: đến hạn ngay.
    bool due(std::uint64_t now) {
        if (!started_) {
            next_due_ = now;
            started_  = true;
        }
        return now >= next_due_;
    }
    // Gọi sau khi chạy xong (thời điểm `after`). Trả true nếu đã lỡ ít nhất một chu kỳ (overrun).
    bool advance(std::uint64_t after, std::uint32_t period) {
        const bool overrun = after >= next_due_ + period;
        next_due_          = overrun ? after + period : next_due_ + period;
        return overrun;
    }
    // Số ms tới hạn kế tiếp, tối đa `cap`.
    std::uint32_t wait(std::uint64_t now, std::uint32_t cap) const {
        if (!started_ || next_due_ <= now) return 0;
        const std::uint64_t d = next_due_ - now;
        return d < cap ? static_cast<std::uint32_t>(d) : cap;
    }
    void reset() { started_ = false; }

private:
    std::uint64_t next_due_ = 0;
    bool started_           = false;
};

}  // namespace svc
