#pragma once
#include <cstdint>

#include "core/status.hpp"

namespace hal {

enum class PinMode : std::uint8_t { Input, InputPullUp, InputPullDown, Output, OutputOpenDrain };

class IGpio {
public:
    virtual ~IGpio() = default;
    virtual core::Status configure(PinMode mode) = 0;
    virtual core::Status write(bool level)       = 0;
    virtual bool read() const                    = 0;
    virtual core::Status toggle() { return write(!read()); }
};

}  // namespace hal
