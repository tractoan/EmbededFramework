#pragma once
// Ngoại vi HAL riêng của Linux (user space), dùng bởi linux_sim, zynqmp_linux và các SBC khác
// (Raspberry Pi, i.MX...). Chỉ dùng giao diện chuẩn của kernel:
//   I2cDev        : /dev/i2c-N (ioctl I2C_RDWR, có repeated start)
//   GpioLine      : GPIO character device v2 (/dev/gpiochipN), chọn chip theo đường dẫn hoặc nhãn
// Phần POSIX chung (StdioUart, TermiosUart, FileStorage, env_or) ở posix/posix_hal.hpp.
#include "posix/posix_hal.hpp"

namespace hal::host {

class I2cDev final : public II2c {
public:
    explicit I2cDev(const char* path) : path_(path) {}
    ~I2cDev() override;
    core::Status open();
    bool is_open() const { return fd_ >= 0; }
    const char* path() const { return path_; }

    core::Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    core::Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    core::Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                            std::size_t rx_len, std::uint32_t timeout_ms) override;

private:
    const char* path_;
    int fd_ = -1;
};

// Một đường GPIO. `chip` là đường dẫn (/dev/gpiochip0) hoặc nhãn chip (vd. "zynqmp_gpio").
class GpioLine final : public IGpio {
public:
    GpioLine(const char* chip, unsigned offset, const char* consumer) : chip_(chip), offset_(offset), consumer_(consumer) {}
    ~GpioLine() override;
    core::Status configure(PinMode mode) override;
    core::Status write(bool level) override;
    bool read() const override;
    bool is_open() const { return line_fd_ >= 0; }

private:
    const char* chip_;
    unsigned offset_;
    const char* consumer_;
    int line_fd_ = -1;
    int abi_     = 2;       // GPIO uAPI của line_fd_ (1 hoặc 2)
    bool output_ = false;
};

}  // namespace hal::host
