#pragma once
#include "hal/i2c.hpp"
#include "services/drivers/temperature_sensor.hpp"

namespace svc {

// Driver TMP102 (TI) – chỉ dùng hal::II2c nên chạy được trên mọi board.
class Tmp102 final : public ITemperatureSensor {
public:
    void init(hal::II2c* bus, std::uint8_t addr = 0x48) {
        bus_  = bus;
        addr_ = addr;
    }
    core::Status read_deci_celsius(std::int32_t& out) override;

private:
    hal::II2c* bus_     = nullptr;
    std::uint8_t addr_  = 0x48;
};

}  // namespace svc
