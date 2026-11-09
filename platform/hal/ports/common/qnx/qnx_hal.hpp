#pragma once
// Ngoại vi HAL riêng của QNX Neutrino (7.x, 8.x). Phần POSIX chung (StdioUart, TermiosUart cho
// /dev/serN, FileStorage, env_or) ở posix/posix_hal.hpp.
//   QnxI2c   : resource manager I2C (/dev/i2cN – i2c-imx, i2c-cdns, i2c-omap...) qua devctl()
//              DCMD_I2C_SEND / RECV / SENDRECV (repeated start). Driver đã serialize các client.
//   MmioGpio : QNX không có API GPIO chuẩn -> ánh xạ thanh ghi của khối GPIO (mmap_device_memory,
//              cần quyền PROCMGR_AID_MEM_PHYS / root) và dùng mmio::GpioPin.
#include <cstddef>
#include <cstdint>

#include "mmio/mmio_gpio.hpp"
#include "posix/posix_hal.hpp"

namespace hal::host {

class QnxI2c final : public II2c {
public:
    explicit QnxI2c(const char* path, std::uint32_t bus_speed_hz = 100000) : path_(path), speed_(bus_speed_hz) {}
    ~QnxI2c() override;
    core::Status open();
    bool is_open() const { return fd_ >= 0; }
    const char* path() const { return path_; }

    core::Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    core::Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    core::Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                            std::size_t rx_len, std::uint32_t timeout_ms) override;

    static constexpr std::size_t kMaxTransfer = 256;   // byte mỗi chiều; buffer trên stack, không heap

private:
    const char* path_;
    std::uint32_t speed_;
    int fd_ = -1;
};

// Một chân GPIO cấu hình bằng chuỗi mmio (xem mmio_gpio.hpp). Chuỗi rỗng/nullptr = chân không dùng.
class MmioGpio final : public IGpio {
public:
    explicit MmioGpio(const char* spec) : spec_text_(spec) {}
    ~MmioGpio() override;
    core::Status configure(PinMode mode) override;   // lần đầu: phân tích chuỗi + ánh xạ thanh ghi
    core::Status write(bool level) override { return pin_.write(level); }
    bool read() const override { return pin_.read(); }
    const char* spec() const { return spec_text_ ? spec_text_ : ""; }

private:
    const char* spec_text_;
    mmio::GpioPin pin_;
    void* map_         = nullptr;
    std::size_t map_len_ = 0;
};

}  // namespace hal::host
