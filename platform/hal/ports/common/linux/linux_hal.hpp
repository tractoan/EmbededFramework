#pragma once
// Ngoại vi HAL cho mọi board chạy Linux (user space): dùng chung bởi linux_sim, zynqmp_linux
// và các SBC khác (Raspberry Pi, i.MX...). Chỉ dùng giao diện chuẩn của kernel:
//   StdioUart     : console = stdin/stdout của tiến trình
//   TermiosUart   : /dev/ttyXXX (termios, raw mode)
//   I2cDev        : /dev/i2c-N (ioctl I2C_RDWR, có repeated start)
//   GpioLine      : GPIO character device v2 (/dev/gpiochipN), chọn chip theo đường dẫn hoặc nhãn
//   FileStorage   : file thường, hoặc file EEPROM của kernel (/sys/bus/i2c/devices/1-0054/eeprom)
// Không cấp phát heap sau init.
#include <atomic>

#include "hal/board.hpp"

namespace hal::host {

class StdioUart final : public IUart {
public:
    core::Status open(const UartConfig& cfg) override;
    void close() override {}
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    bool eof_ = false;
};

class TermiosUart final : public IUart {
public:
    explicit TermiosUart(const char* path) : path_(path) {}
    ~TermiosUart() override { close(); }
    core::Status open(const UartConfig& cfg) override;
    void close() override;
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    bool is_open() const { return fd_ >= 0; }
    const char* path() const { return path_; }

private:
    const char* path_;
    int fd_ = -1;
};

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
    bool output_ = false;
};

class FileStorage final : public INvStorage {
public:
    FileStorage(const char* path, std::size_t size) : path_(path), size_(size) {}
    std::size_t size() const override { return size_; }
    core::Status read(std::size_t offset, void* data, std::size_t len) override;
    core::Status write(std::size_t offset, const void* data, std::size_t len) override;
    core::Status erase() override;

private:
    const char* path_;
    std::size_t size_;
};

// Tiện ích cấu hình: biến môi trường (nếu có) ghi đè giá trị mặc định lúc build.
const char* env_or(const char* name, const char* fallback);
unsigned env_or(const char* name, unsigned fallback);

}  // namespace hal::host
