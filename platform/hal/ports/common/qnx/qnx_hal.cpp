#include "qnx/qnx_hal.hpp"

#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <hw/i2c.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace hal::host {

using core::Status;

// ---------------------------------------------------------------- QnxI2c
namespace {
// devctl() trả về EOK (0) hoặc mã errno (KHÔNG phải -1).
Status from_errno(int err) {
    switch (err) {
        case EOK:       return Status::Ok;
        case ETIMEDOUT: return Status::Timeout;
        case EBUSY:
        case EAGAIN:    return Status::Busy;
        case EINVAL:    return Status::InvalidArg;
        case ENOTSUP:
        case ENOTTY:    return Status::NotSupported;
        default:        return Status::Error;   // EIO: NACK, mất arbitration...
    }
}

i2c_addr_t slave_of(std::uint8_t addr) {
    i2c_addr_t a{};
    a.addr = addr;
    a.fmt  = I2C_ADDRFMT_7BIT;
    return a;
}
}  // namespace

QnxI2c::~QnxI2c() {
    if (fd_ >= 0) ::close(fd_);
}

Status QnxI2c::open() {
    if (fd_ >= 0) return Status::Ok;
    fd_ = ::open(path_, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) return Status::NotFound;
    if (speed_ != 0) {
        std::uint32_t speed = speed_;
        // Không phải driver nào cũng cho đổi tốc độ (có thể cố định bằng tham số khi khởi động driver).
        (void)::devctl(fd_, DCMD_I2C_SET_BUS_SPEED, &speed, sizeof(speed), nullptr);
    }
    return Status::Ok;
}

// Timeout do driver I2C của QNX quản lý; tham số timeout_ms không dùng.
Status QnxI2c::write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    if (fd_ < 0) return Status::NotSupported;
    if (len > kMaxTransfer || (len > 0 && data == nullptr)) return Status::InvalidArg;
    struct {
        i2c_send_t hdr;
        std::uint8_t buf[kMaxTransfer];
    } msg{};
    msg.hdr.slave = slave_of(addr);
    msg.hdr.len   = static_cast<std::uint32_t>(len);
    msg.hdr.stop  = 1;
    if (len > 0) std::memcpy(msg.buf, data, len);
    return from_errno(::devctl(fd_, DCMD_I2C_SEND, &msg, sizeof(msg.hdr) + len, nullptr));
}

Status QnxI2c::read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    if (fd_ < 0) return Status::NotSupported;
    if (len == 0 || len > kMaxTransfer || data == nullptr) return Status::InvalidArg;
    struct {
        i2c_recv_t hdr;
        std::uint8_t buf[kMaxTransfer];
    } msg{};
    msg.hdr.slave = slave_of(addr);
    msg.hdr.len   = static_cast<std::uint32_t>(len);
    msg.hdr.stop  = 1;
    const Status st = from_errno(::devctl(fd_, DCMD_I2C_RECV, &msg, sizeof(msg.hdr) + len, nullptr));
    if (core::is_ok(st)) std::memcpy(data, msg.buf, len);
    return st;
}

Status QnxI2c::write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                          std::size_t rx_len, std::uint32_t /*timeout_ms*/) {
    if (fd_ < 0) return Status::NotSupported;
    if (tx_len > kMaxTransfer || rx_len > kMaxTransfer || rx_len == 0 || rx == nullptr || (tx_len > 0 && tx == nullptr)) {
        return Status::InvalidArg;
    }
    // Một giao dịch: gửi tx_len byte, repeated start, nhận rx_len byte; dữ liệu nhận ghi đè lên buffer.
    struct {
        i2c_sendrecv_t hdr;
        std::uint8_t buf[kMaxTransfer];
    } msg{};
    msg.hdr.slave    = slave_of(addr);
    msg.hdr.send_len = static_cast<std::uint32_t>(tx_len);
    msg.hdr.recv_len = static_cast<std::uint32_t>(rx_len);
    msg.hdr.stop     = 1;
    if (tx_len > 0) std::memcpy(msg.buf, tx, tx_len);
    const std::size_t body = tx_len > rx_len ? tx_len : rx_len;
    const Status st        = from_errno(::devctl(fd_, DCMD_I2C_SENDRECV, &msg, sizeof(msg.hdr) + body, nullptr));
    if (core::is_ok(st)) std::memcpy(rx, msg.buf, rx_len);
    return st;
}

// -------------------------------------------------------------- MmioGpio
MmioGpio::~MmioGpio() {
    if (map_ != nullptr) ::munmap_device_memory(map_, map_len_);
}

Status MmioGpio::configure(PinMode mode) {
    if (!pin_.attached()) {
        mmio::GpioSpec spec;
        if (spec_text_ == nullptr || spec_text_[0] == '\0') return Status::NotSupported;   // chân không cấu hình
        if (!mmio::parse_gpio_spec(spec_text_, spec)) return Status::InvalidArg;
        // Ánh xạ theo trang (base có thể không căn trang), không cache (thanh ghi thiết bị).
        const std::uint64_t page  = static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
        const std::uint64_t start = spec.base & ~(page - 1u);
        const std::size_t len     = static_cast<std::size_t>(spec.base - start) + spec.span();
        void* p = ::mmap_device_memory(nullptr, len, PROT_READ | PROT_WRITE | PROT_NOCACHE, 0, start);
        if (p == MAP_FAILED) {
            if (errno == EPERM) {   // không có Status riêng cho "thiếu quyền": báo rõ nguyên nhân ở đây
                std::fputs("[qnx] mmap_device_memory: permission denied (run as root or grant PROCMGR_AID_MEM_PHYS)\n",
                           stderr);
                return Status::NotSupported;
            }
            return Status::NotFound;   // địa chỉ vật lý không hợp lệ
        }
        map_     = p;
        map_len_ = len;
        pin_.attach(reinterpret_cast<volatile std::uint32_t*>(static_cast<std::uint8_t*>(p) + (spec.base - start)), spec);
    }
    return pin_.configure(mode);
}

}  // namespace hal::host
