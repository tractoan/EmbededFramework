#pragma once
#include <cstdint>

#include "core/status.hpp"

namespace device {

// Interface thiết bị (Device Abstraction): nghiệp vụ phụ thuộc interface này,
// không phụ thuộc chip cụ thể (TMP102, LM75, NTC+ADC, Modbus sensor...).
class ITemperatureSensor {
public:
    virtual ~ITemperatureSensor() = default;
    virtual core::Status read_deci_celsius(std::int32_t& out) = 0;   // đơn vị 0.1 °C
};

}  // namespace device
