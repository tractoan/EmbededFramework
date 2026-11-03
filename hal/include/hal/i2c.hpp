#pragma once
#include <cstddef>
#include <cstdint>

#include "core/status.hpp"

namespace hal {

// I2C master. addr là địa chỉ 7-bit (chưa dịch).
class II2c {
public:
    virtual ~II2c() = default;
    virtual core::Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) = 0;
    virtual core::Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms)        = 0;
    virtual core::Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                                    std::size_t rx_len, std::uint32_t timeout_ms)                                     = 0;
};

}  // namespace hal
