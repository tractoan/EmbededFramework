// Board port: QNX Neutrino (7.1 / 8.0) trên bất kỳ board nào có BSP QNX (aarch64le, x86_64):
// i.MX8, Zynq UltraScale+, TI AM6x/J7, Raspberry Pi 4, Renesas R-Car...
// Driver là resource manager của BSP; phần này chỉ ánh xạ ID logic -> thiết bị.
//
// Mặc định (ghi đè lúc chạy bằng biến môi trường, không cần build lại):
//   Console    : stdin/stdout của tiến trình
//   EP_QNX_AUX_UART    : /dev/ser2   (devc-ser*; "" = tắt)
//   EP_QNX_SENSOR_I2C  : /dev/i2c1   (i2c-*; cảm biến TMP102)    EP_QNX_I2C_SPEED: 100000 (Hz)
//   EP_QNX_GPIO_STATUS_LED, EP_QNX_GPIO_ALARM_LED, EP_QNX_GPIO_BUTTON:
//                        chuỗi thanh ghi GPIO (xem common/mmio/mmio_gpio.hpp); trống = không dùng
//   EP_QNX_NVRAM       : /data/embededframework/nvram.bin (cần filesystem ghi được, bền vững – qnx6fs)
// Ví dụ ZCU102: EP_QNX_GPIO_STATUS_LED="base=0xFF0A0000,out=0x40,in=0x60,dir=0x204,oen=0x208,bit=23"
#include <cstdio>
#include <cstdlib>

#include "qnx/qnx_hal.hpp"

#ifndef EP_QNX_DEFAULT_AUX_UART
#define EP_QNX_DEFAULT_AUX_UART "/dev/ser2"
#endif
#ifndef EP_QNX_DEFAULT_SENSOR_I2C
#define EP_QNX_DEFAULT_SENSOR_I2C "/dev/i2c1"
#endif
#ifndef EP_QNX_DEFAULT_I2C_SPEED
#define EP_QNX_DEFAULT_I2C_SPEED 100000u
#endif
#ifndef EP_QNX_DEFAULT_GPIO_STATUS_LED
#define EP_QNX_DEFAULT_GPIO_STATUS_LED ""
#endif
#ifndef EP_QNX_DEFAULT_GPIO_ALARM_LED
#define EP_QNX_DEFAULT_GPIO_ALARM_LED ""
#endif
#ifndef EP_QNX_DEFAULT_GPIO_BUTTON
#define EP_QNX_DEFAULT_GPIO_BUTTON ""
#endif
#ifndef EP_QNX_DEFAULT_NVRAM
#define EP_QNX_DEFAULT_NVRAM "/data/embededframework/nvram.bin"
#endif

namespace hal {

namespace {

class QnxBoard final : public IBoard {
public:
    core::Status init() override {
        // Thiết bị thiếu không làm hỏng khởi động: ngoại vi đó trả lỗi khi dùng.
        warn_if(status_led_.configure(PinMode::Output), "status LED", status_led_.spec());
        warn_if(alarm_led_.configure(PinMode::Output), "alarm LED", alarm_led_.spec());
        warn_if(button_.configure(PinMode::InputPullUp), "user button", button_.spec());   // pull-up: pinmux của BSP
        warn_if(sensor_i2c_.open(), "sensor I2C", sensor_i2c_.path());
        if (aux_uart_.path()[0] != '\0') {
            aux_ok_ = core::is_ok(aux_uart_.open(UartConfig{}));
            warn_if(aux_ok_ ? core::Status::Ok : core::Status::NotFound, "aux UART", aux_uart_.path());
        }
        return console_.open(UartConfig{});
    }

    const char* name() const override { return "qnx"; }

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
        // "Reset board" = kết thúc tiến trình; SLM (System Launch and Monitor) hoặc script khởi động lại.
        std::fputs("board reset requested: exiting\n", stderr);
        std::fflush(nullptr);
        std::_Exit(EXIT_FAILURE);
    }

private:
    static void warn_if(core::Status st, const char* what, const char* where) {
        if (!core::is_ok(st)) {
            std::fprintf(stderr, "[qnx] %s not available (%s): %s\n", what, where[0] ? where : "not configured",
                         core::to_string(st));
        }
    }
    static const char* env_raw(const char* name, const char* fallback) {   // "" hợp lệ (= tắt)
        const char* v = std::getenv(name);
        return v != nullptr ? v : fallback;
    }

    host::MmioGpio status_led_{host::env_or("EP_QNX_GPIO_STATUS_LED", EP_QNX_DEFAULT_GPIO_STATUS_LED)};
    host::MmioGpio alarm_led_{host::env_or("EP_QNX_GPIO_ALARM_LED", EP_QNX_DEFAULT_GPIO_ALARM_LED)};
    host::MmioGpio button_{host::env_or("EP_QNX_GPIO_BUTTON", EP_QNX_DEFAULT_GPIO_BUTTON)};
    host::StdioUart console_;
    host::TermiosUart aux_uart_{env_raw("EP_QNX_AUX_UART", EP_QNX_DEFAULT_AUX_UART)};
    bool aux_ok_ = false;
    host::QnxI2c sensor_i2c_{host::env_or("EP_QNX_SENSOR_I2C", EP_QNX_DEFAULT_SENSOR_I2C),
                             host::env_or("EP_QNX_I2C_SPEED", EP_QNX_DEFAULT_I2C_SPEED)};
    host::FileStorage storage_{host::env_or("EP_QNX_NVRAM", EP_QNX_DEFAULT_NVRAM), 4096};
};

}  // namespace

IBoard& board() {
    static QnxBoard instance;
    return instance;
}

}  // namespace hal
