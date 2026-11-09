#include <cstdio>
#include <cstdlib>

#include "linux/linux_hal.hpp"
#include "sim_bringup.hpp"
#include "sim_peripherals.hpp"

namespace hal {

namespace {

class LinuxSimBoard final : public IBoard {
public:
    core::Status init() override {
        status_led_.configure(PinMode::Output);
        alarm_led_.configure(PinMode::Output);
        button_.configure(PinMode::InputPullUp);
        bringup_.init();
        if (!core::is_ok(aux_.open(UartConfig{}))) {
            std::fprintf(stderr, "[SIM] aux UART: cannot bind udp port %u (disabled)\n", aux_.port());
            aux_ok_ = false;
        }
        return console_.open(UartConfig{});
    }

    const char* name() const override { return "linux-sim"; }

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
        if (id == UartId::Aux && aux_ok_) return &aux_;
        return nullptr;
    }
    II2c* i2c(I2cId id) override { return id == I2cId::Sensor ? &i2c_ : nullptr; }
    INvStorage* storage() override { return &storage_; }

    std::size_t serial_count() const override { return sim::SimBringupHw::kSerials; }
    IUart* serial(std::size_t i) override { return bringup_.serial(i); }
    std::size_t digital_input_count() const override { return sim::SimBringupHw::kDigital; }
    IGpio* digital_input(std::size_t i) override { return bringup_.input(i); }
    std::size_t digital_output_count() const override { return sim::SimBringupHw::kDigital; }
    IGpio* digital_output(std::size_t i) override { return bringup_.output(i); }
    std::size_t hdlc_count() const override { return sim::SimBringupHw::kHdlc; }
    IHdlcPort* hdlc(std::size_t i) override { return bringup_.hdlc(i); }

    [[noreturn]] void reset() override {
        std::fputs("[SIM] board reset\n", stdout);
        std::fflush(stdout);
        std::_Exit(0);
    }

private:
    sim::SimGpio status_led_{"StatusLed", false};   // heartbeat -> không in để tránh spam
    sim::SimGpio alarm_led_{"AlarmLed", false};   // thay đổi pattern đã được log bởi LedService
    sim::SimGpio button_{"UserButton", true};
    host::StdioUart console_;   // console = terminal
    sim::SimUdpUart aux_{15020};
    bool aux_ok_ = true;
    sim::SimI2c i2c_;
    host::FileStorage storage_{host::env_or("EP_SIM_NVRAM", "ep_nvram.bin"), 4096};   // "Flash" = file
    sim::SimBringupHw bringup_;   // 12 serial, 20 DI/DO, 2 HDLC-IMU (sản phẩm hw_bringup)
};

}  // namespace

IBoard& board() {
    static LinuxSimBoard instance;
    return instance;
}

}  // namespace hal
