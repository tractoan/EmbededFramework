#include "linux/linux_hal.hpp"
#include "linux/gpio_uapi.hpp"

#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace hal::host {

using core::Status;

// ---------------------------------------------------------------- I2cDev
I2cDev::~I2cDev() {
    if (fd_ >= 0) ::close(fd_);
}

Status I2cDev::open() {
    if (fd_ >= 0) return Status::Ok;
    fd_ = ::open(path_, O_RDWR | O_CLOEXEC);
    return fd_ >= 0 ? Status::Ok : Status::NotFound;
}

namespace {
Status transfer(int fd, i2c_msg* msgs, unsigned n) {
    if (fd < 0) return Status::NotSupported;
    i2c_rdwr_ioctl_data d{msgs, n};
    return ::ioctl(fd, I2C_RDWR, &d) == static_cast<int>(n) ? Status::Ok : Status::Error;   // NACK / bus lỗi
}
}  // namespace

// Timeout I2C do driver kernel quản lý (adapter timeout), tham số timeout_ms không dùng.
Status I2cDev::write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    i2c_msg m{addr, 0, static_cast<__u16>(len), const_cast<__u8*>(data)};
    return transfer(fd_, &m, 1);
}

Status I2cDev::read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    i2c_msg m{addr, I2C_M_RD, static_cast<__u16>(len), data};
    return transfer(fd_, &m, 1);
}

Status I2cDev::write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                          std::size_t rx_len, std::uint32_t /*timeout_ms*/) {
    i2c_msg m[2] = {{addr, 0, static_cast<__u16>(tx_len), const_cast<__u8*>(tx)},
                    {addr, I2C_M_RD, static_cast<__u16>(rx_len), rx}};   // 2 message = repeated start
    return transfer(fd_, m, 2);
}

// -------------------------------------------------------------- GpioLine
namespace {
// Mở chip theo đường dẫn ("/dev/...") hoặc tìm theo nhãn trong /dev/gpiochip0..31.
int open_chip(const char* chip) {
    if (chip == nullptr) return -1;
    if (chip[0] == '/') return ::open(chip, O_RDWR | O_CLOEXEC);
    for (int i = 0; i < 32; ++i) {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
        const int fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        gpiochip_info info{};
        if (::ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0 && std::strncmp(info.label, chip, sizeof(info.label)) == 0) {
            return fd;
        }
        ::close(fd);
    }
    return -1;
}
}  // namespace

GpioLine::~GpioLine() {
    if (line_fd_ >= 0) ::close(line_fd_);
}

Status GpioLine::configure(PinMode mode) {
    if (line_fd_ >= 0) ::close(line_fd_);
    line_fd_ = -1;
    const int chip_fd = open_chip(chip_);
    if (chip_fd < 0) return Status::NotFound;

    gpio::LineRequest req;   // uAPI v2 hoặc v1 tùy header/kernel (linux/gpio_uapi.hpp)
    req.offset     = offset_;
    req.consumer   = consumer_;
    output_        = mode == PinMode::Output || mode == PinMode::OutputOpenDrain;
    req.output     = output_;
    req.open_drain = mode == PinMode::OutputOpenDrain;
    req.bias       = mode == PinMode::InputPullUp ? gpio::Bias::PullUp
                     : mode == PinMode::InputPullDown ? gpio::Bias::PullDown : gpio::Bias::None;
    const int fd = gpio::request_line(chip_fd, req, abi_);
    ::close(chip_fd);   // giữ fd của line, không cần fd của chip
    if (fd < 0) return Status::Busy;   // line không tồn tại hoặc đang được dùng
    line_fd_ = fd;
    return Status::Ok;
}

Status GpioLine::write(bool level) {
    if (line_fd_ < 0 || !output_) return Status::NotSupported;
    return gpio::set_value(line_fd_, abi_, level) ? Status::Ok : Status::Error;
}

bool GpioLine::read() const {
    if (line_fd_ < 0) return false;
    bool level = false;
    return gpio::get_value(line_fd_, abi_, level) && level;
}

}  // namespace hal::host
