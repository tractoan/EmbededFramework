#pragma once
// Peripheral THẬT trên Linux (CPU nhúng x86/ARM chạy Linux) riêng của board bring-up: tty có RS485,
// GPIO character device v2 có bias/active-low, cổng SDLC/HDLC (SyncLink/N_HDLC, chardev, netdev generic-HDLC).
// Console stdio và Flash = file dùng chung: platform/hal/ports/common/linux (hal::host).
#include <atomic>
#include <string>
#include <utility>

#include "hal/board.hpp"

namespace hal::linux_cpu {

// Cổng serial qua tty. Spec: "/dev/ttyS4[:rs485|:rs485-echo]"
//   rs485      : bật chế độ RS485 của driver (RTS điều khiển hướng)
//   rs485-echo : như trên + nhận cả dữ liệu mình phát (SER_RS485_RX_DURING_TX) -> loopback 2 dây trên 1 cổng
class LinuxSerial final : public IUart {
public:
    explicit LinuxSerial(std::string spec);
    ~LinuxSerial() override { close(); }
    core::Status open(const UartConfig& cfg) override;
    void close() override;
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    const std::string& spec() const { return spec_; }

private:
    std::string spec_, path_;
    bool rs485_ = false, rs485_echo_ = false;
    std::atomic<int> fd_{-1};
};

// Một chân GPIO qua /dev/gpiochipN (uAPI v2). Spec: "gpiochip0:17[:low]"  (low = tích cực mức thấp)
class LinuxGpio final : public IGpio {
public:
    LinuxGpio(std::string spec, bool output);
    ~LinuxGpio() override;
    core::Status configure(PinMode mode) override;
    core::Status write(bool level) override;
    bool read() const override;

private:
    std::string spec_;
    bool output_;
    int line_fd_ = -1;
    bool level_  = false;   // giá trị ghi gần nhất (output)
};

// Cổng HDLC. Spec:
//   synclink:/dev/ttySLG0  card Microgate SyncLink (driver synclink_gt): cấu hình bằng MGSL_IOCSPARAMS + N_HDLC
//   nhdlc:/dev/ttyXXX      tty bất kỳ đã cấu hình đồng bộ từ ngoài, chỉ gắn line discipline N_HDLC
//   chardev:/dev/xxx       driver ký tự trả MỘT khung mỗi lần read() (vd. lõi HDLC trong FPGA)
//   packet:hdlc0           netdev generic-HDLC (cấu hình trước: sethdlc hdlc0 ...; ip link set hdlc0 up)
class LinuxHdlc final : public IHdlcPort {
public:
    explicit LinuxHdlc(std::string spec);
    ~LinuxHdlc() override { close(); }
    core::Status open(const HdlcConfig& cfg) override;
    void close() override;
    std::size_t read_frame(std::uint8_t* buf, std::size_t cap, std::uint32_t timeout_ms, HdlcFrameStatus* st) override;
    core::Status write_frame(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    core::Status open_synclink(const std::string& path, const HdlcConfig& cfg);
    std::string spec_, kind_, target_;
    std::atomic<int> fd_{-1};
};

}  // namespace hal::linux_cpu
