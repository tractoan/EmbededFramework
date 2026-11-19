#pragma once
#include <atomic>

#include "device/temperature_sensor.hpp"
#include "hal/i2c.hpp"

namespace device {

// Cấu hình TMP102 (thanh ghi Configuration, con trỏ 0x01).
struct Tmp102Config {
    enum class Rate : std::uint8_t { Hz0_25 = 0, Hz1 = 1, Hz4 = 2, Hz8 = 3 };   // CR1:CR0, mặc định 4 Hz
    Rate rate     = Rate::Hz4;
    bool extended = false;   // EM: 13 bit, đo tới +150 °C (mặc định 12 bit, tới +128 °C)
    bool shutdown = false;   // SD: ngừng chuyển đổi (tiết kiệm năng lượng); đọc trả giá trị cuối
};

// Driver TMP102 (TI) – chỉ dùng hal::II2c nên chạy được trên mọi board.
class Tmp102 final : public ITemperatureSensor {
public:
    void init(hal::II2c* bus, std::uint8_t addr = 0x48) {
        bus_  = bus;
        addr_ = addr;
    }
    core::Status read_deci_celsius(std::int32_t& out) override;
    // Đọc-sửa-ghi thanh ghi cấu hình (giữ các bit khác: phân cực, ngưỡng cảnh báo...). Lỗi bus -> giữ cấu hình cũ.
    core::Status apply(const Tmp102Config& cfg);
    bool extended() const { return extended_.load(); }

private:
    hal::II2c* bus_     = nullptr;
    std::uint8_t addr_  = 0x48;
    std::atomic<bool> extended_{false};   // định dạng dữ liệu nhiệt độ phụ thuộc EM
};

}  // namespace device
