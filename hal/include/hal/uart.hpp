#pragma once
#include <cstddef>
#include <cstdint>

#include "core/status.hpp"

namespace hal {

struct UartConfig {
    enum class Parity : std::uint8_t { None, Even, Odd };
    std::uint32_t baudrate = 115200;
    std::uint8_t data_bits = 8;
    Parity parity          = Parity::None;
    std::uint8_t stop_bits = 1;
};

class IUart {
public:
    virtual ~IUart() = default;
    virtual core::Status open(const UartConfig& cfg) = 0;
    virtual void close()                             = 0;
    // Trả về số byte thực sự ghi/đọc được (0 khi timeout).
    virtual std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) = 0;
    virtual std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms)        = 0;
};

}  // namespace hal
