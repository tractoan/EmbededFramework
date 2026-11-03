#include <cstdio>
#include <cstdlib>

#include "sim_peripherals.hpp"

namespace hal {

namespace {

class LinuxSimBoard final : public IBoard {
public:
    core::Status init() override {
        status_led_.configure(PinMode::Output);
        alarm_led_.configure(PinMode::Output);
        button_.configure(PinMode::InputPullUp);
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

    IUart* uart(UartId id) override { return id == UartId::Console ? &console_ : nullptr; }
    II2c* i2c(I2cId id) override { return id == I2cId::Sensor ? &i2c_ : nullptr; }
    INvStorage* storage() override { return &storage_; }

    [[noreturn]] void reset() override {
        std::fputs("[SIM] board reset\n", stdout);
        std::fflush(stdout);
        std::_Exit(0);
    }

private:
    sim::SimGpio status_led_{"StatusLed", false};   // heartbeat -> không in để tránh spam
    sim::SimGpio alarm_led_{"AlarmLed", false};   // thay đổi pattern đã được log bởi LedService
    sim::SimGpio button_{"UserButton", true};
    sim::SimConsoleUart console_;
    sim::SimI2c i2c_;
    sim::SimFileStorage storage_{4096};
};

}  // namespace

IBoard& board() {
    static LinuxSimBoard instance;
    return instance;
}

}  // namespace hal
