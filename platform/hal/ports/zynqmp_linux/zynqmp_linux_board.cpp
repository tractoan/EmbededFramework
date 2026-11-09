// Board port: Zynq UltraScale+ MPSoC – APU (Cortex-A53) chạy Linux (PetaLinux / Yocto).
// Dùng driver Linux chuẩn qua ports/common/linux; chỉ phần ánh xạ ID logic -> thiết bị nằm ở đây.
//
// Mặc định theo ZCU102 – kiểm tra lại với device tree / schematic của board:
//   Console    : stdin/stdout của tiến trình (chạy trên console ttyPS0 hoặc qua ssh)
//   Aux UART   : /dev/ttyPS1 (PS UART1)
//   Sensor I2C : /dev/i2c-1 (PS I2C1; cảm biến TMP102 gắn ngoài, vd. qua PMOD)
//   GPIO       : chip "zynqmp_gpio" (PS GPIO): MIO23 = LED trạng thái, MIO22 = nút nhấn,
//                EMIO0 (line 78) = LED cảnh báo – cần design PL nối EMIO0 tới LED
//   NV storage : /var/lib/embededframework/nvram.bin (hoặc file EEPROM của kernel,
//                vd. /sys/bus/i2c/devices/0-0054/eeprom)
//
// Ghi đè lúc chạy (không cần build lại): EP_ZYNQMP_AUX_UART, EP_ZYNQMP_SENSOR_I2C, EP_ZYNQMP_GPIOCHIP
// (đường dẫn hoặc nhãn), EP_ZYNQMP_LED_STATUS, EP_ZYNQMP_LED_ALARM, EP_ZYNQMP_BUTTON (số line),
// EP_ZYNQMP_NVRAM. Đặt EP_ZYNQMP_AUX_UART="" để tắt cổng phụ.
//
// Hệ AMP (Linux trên APU + FreeRTOS trên RPU): mỗi ngoại vi chỉ được một bên dùng – vd. APU giữ
// UART0 + I2C0, RPU giữ UART1 + I2C1; cấu hình bằng các biến trên và device tree.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "linux/linux_hal.hpp"

#ifndef EP_ZYNQMP_DEFAULT_AUX_UART
#define EP_ZYNQMP_DEFAULT_AUX_UART "/dev/ttyPS1"
#endif
#ifndef EP_ZYNQMP_DEFAULT_SENSOR_I2C
#define EP_ZYNQMP_DEFAULT_SENSOR_I2C "/dev/i2c-1"
#endif
#ifndef EP_ZYNQMP_DEFAULT_GPIOCHIP
#define EP_ZYNQMP_DEFAULT_GPIOCHIP "zynqmp_gpio"
#endif
#ifndef EP_ZYNQMP_DEFAULT_LED_STATUS
#define EP_ZYNQMP_DEFAULT_LED_STATUS 23u   // MIO23
#endif
#ifndef EP_ZYNQMP_DEFAULT_LED_ALARM
#define EP_ZYNQMP_DEFAULT_LED_ALARM 78u    // EMIO0 (MIO 0..77, EMIO bắt đầu từ 78)
#endif
#ifndef EP_ZYNQMP_DEFAULT_BUTTON
#define EP_ZYNQMP_DEFAULT_BUTTON 22u       // MIO22
#endif
#ifndef EP_ZYNQMP_DEFAULT_NVRAM
#define EP_ZYNQMP_DEFAULT_NVRAM "/var/lib/embededframework/nvram.bin"
#endif

namespace hal {

namespace {

class ZynqMpLinuxBoard final : public IBoard {
public:
    core::Status init() override {
        // Thiết bị thiếu không làm hỏng khởi động: ngoại vi đó trả lỗi khi dùng
        // (vd. không có cảm biến -> TemperatureMonitor báo lỗi cảm biến, cảnh báo vào FAULT).
        warn_if(status_led_.configure(PinMode::Output), "status LED", gpiochip_);
        warn_if(alarm_led_.configure(PinMode::Output), "alarm LED", gpiochip_);
        warn_if(button_.configure(PinMode::InputPullUp), "user button", gpiochip_);
        warn_if(sensor_i2c_.open(), "sensor I2C", sensor_i2c_.path());
        if (aux_uart_.path()[0] != '\0') {
            aux_ok_ = core::is_ok(aux_uart_.open(UartConfig{}));
            warn_if(aux_ok_ ? core::Status::Ok : core::Status::NotFound, "aux UART", aux_uart_.path());
        }
        return console_.open(UartConfig{});
    }

    const char* name() const override { return "zynqmp-a53-linux"; }

    IGpio* gpio(GpioId id) override {
        switch (id) {
            case GpioId::StatusLed:  return &status_led_;
            case GpioId::AlarmLed:   return &alarm_led_;
            case GpioId::UserButton: return &button_;
            default:                 return nullptr;
        }
    }
    IUart* uart(UartId id) override {
        if (id == UartId::Console) return &console_;
        if (id == UartId::Aux && aux_ok_) return &aux_uart_;
        return nullptr;
    }
    II2c* i2c(I2cId id) override { return id == I2cId::Sensor ? &sensor_i2c_ : nullptr; }
    INvStorage* storage() override { return &storage_; }

    [[noreturn]] void reset() override {
        // Trên Linux, "reset board" = kết thúc tiến trình; systemd (Restart=always) khởi động lại ứng dụng.
        std::fputs("board reset requested: exiting\n", stderr);
        std::fflush(nullptr);
        std::_Exit(EXIT_FAILURE);
    }

private:
    static void warn_if(core::Status st, const char* what, const char* where) {
        if (!core::is_ok(st)) std::fprintf(stderr, "[zynqmp] %s not available (%s): %s\n", what, where, core::to_string(st));
    }

    const char* gpiochip_ = host::env_or("EP_ZYNQMP_GPIOCHIP", EP_ZYNQMP_DEFAULT_GPIOCHIP);
    host::GpioLine status_led_{gpiochip_, host::env_or("EP_ZYNQMP_LED_STATUS", EP_ZYNQMP_DEFAULT_LED_STATUS), "ep-status"};
    host::GpioLine alarm_led_{gpiochip_, host::env_or("EP_ZYNQMP_LED_ALARM", EP_ZYNQMP_DEFAULT_LED_ALARM), "ep-alarm"};
    host::GpioLine button_{gpiochip_, host::env_or("EP_ZYNQMP_BUTTON", EP_ZYNQMP_DEFAULT_BUTTON), "ep-button"};
    host::StdioUart console_;
    host::TermiosUart aux_uart_{std::getenv("EP_ZYNQMP_AUX_UART") ? std::getenv("EP_ZYNQMP_AUX_UART")
                                                                  : EP_ZYNQMP_DEFAULT_AUX_UART};
    bool aux_ok_ = false;
    host::I2cDev sensor_i2c_{host::env_or("EP_ZYNQMP_SENSOR_I2C", EP_ZYNQMP_DEFAULT_SENSOR_I2C)};
    host::FileStorage storage_{host::env_or("EP_ZYNQMP_NVRAM", EP_ZYNQMP_DEFAULT_NVRAM), 4096};
};

}  // namespace

IBoard& board() {
    static ZynqMpLinuxBoard instance;
    return instance;
}

}  // namespace hal
