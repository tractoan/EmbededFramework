#include "fdio/fd_uart.hpp"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "hal/wait.hpp"

namespace hal::fdio {

namespace {
void wait_1ms() {
    if (!hal::wait_ms(1)) ::usleep(1000);   // RTOS chưa chạy: chờ bận (Nios HAL) / ngủ (host)
}
bool would_block(long n) { return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR); }
}  // namespace

core::Status FdUart::open(const UartConfig& /*cfg*/) {
    close();
    if (path_ == nullptr || path_[0] == '\0') return core::Status::NotFound;
    fd_ = ::open(path_, O_RDWR | O_NONBLOCK);
    return fd_ >= 0 ? core::Status::Ok : core::Status::NotFound;
}

void FdUart::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

std::size_t FdUart::write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    std::size_t sent      = 0;
    std::uint32_t waited  = 0;
    while (fd_ >= 0 && sent < len) {
        const long n = ::write(fd_, data + sent, len - sent);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (!would_block(n) || waited >= timeout_ms) break;   // lỗi thật, hoặc FIFO đầy quá lâu
        wait_1ms();
        ++waited;
    }
    return sent;
}

std::size_t FdUart::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (fd_ < 0) {   // thiết bị không có: hành xử như UART im lặng (không quay bận)
        for (std::uint32_t i = 0; i < timeout_ms; ++i) wait_1ms();
        return 0;
    }
    std::uint32_t waited = 0;
    for (;;) {
        const long n = ::read(fd_, data, len);
        if (n > 0) return static_cast<std::size_t>(n);
        if (n == 0 && waited >= timeout_ms) return 0;      // EOF / không có dữ liệu
        if (n < 0 && !would_block(n)) return 0;            // lỗi thật
        if (waited >= timeout_ms) return 0;
        wait_1ms();
        ++waited;
    }
}

}  // namespace hal::fdio
