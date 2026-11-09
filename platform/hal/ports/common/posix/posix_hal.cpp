#include "posix/posix_hal.hpp"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace hal::host {

using core::Status;

namespace {
int poll_ms(std::uint32_t ms) { return ms > 60000u ? 60000 : static_cast<int>(ms); }

int wait_fd(int fd, short events, std::uint32_t timeout_ms) {
    pollfd p{fd, events, 0};
    int r;
    do {
        r = ::poll(&p, 1, poll_ms(timeout_ms));
    } while (r < 0 && errno == EINTR);
    return r;
}
}  // namespace

// ------------------------------------------------------------ cấu hình
const char* env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && v[0] != '\0') ? v : fallback;
}

unsigned env_or(const char* name, unsigned fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') return fallback;
    char* end           = nullptr;
    const unsigned long n = std::strtoul(v, &end, 0);
    return (end != v && *end == '\0') ? static_cast<unsigned>(n) : fallback;
}

// ------------------------------------------------------------- StdioUart
Status StdioUart::open(const UartConfig& /*cfg*/) { return Status::Ok; }

std::size_t StdioUart::write(const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    const ssize_t n = ::write(STDOUT_FILENO, data, len);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

std::size_t StdioUart::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (eof_) {   // stdin đã đóng: hành xử như UART im lặng
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    if (wait_fd(STDIN_FILENO, POLLIN, timeout_ms) <= 0) return 0;
    const ssize_t n = ::read(STDIN_FILENO, data, len);
    if (n <= 0) {
        eof_ = true;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

// ----------------------------------------------------------- TermiosUart
namespace {
speed_t to_speed(std::uint32_t baud) {
    switch (baud) {
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 115200:  return B115200;
#if defined(B230400)
        case 230400:  return B230400;
#endif
#if defined(B460800)
        case 460800:  return B460800;
#endif
#if defined(B921600)
        case 921600:  return B921600;
#endif
#if defined(B1000000)
        case 1000000: return B1000000;
#endif
#if defined(B2000000)
        case 2000000: return B2000000;
#endif
#if defined(B3000000)
        case 3000000: return B3000000;
#endif
        default:      return 0;   // tốc độ OS không hỗ trợ
    }
}

// Raw mode theo POSIX (cfmakeraw không có trong chuẩn – vd. thiếu trên một số bản QNX):
// không xử lý ký tự đầu vào/ra, không echo, không tín hiệu, 8 bit.
void make_raw(termios& t) {
    t.c_iflag &= ~static_cast<tcflag_t>(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF);
    t.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    t.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
}
}  // namespace

Status TermiosUart::open(const UartConfig& cfg) {
    close();
    const speed_t speed = to_speed(cfg.baudrate);
    if (speed == 0 || cfg.data_bits < 5 || cfg.data_bits > 8) return Status::InvalidArg;
    fd_ = ::open(path_, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) return Status::NotFound;

    termios t{};
    if (::tcgetattr(fd_, &t) != 0) {
        close();
        return Status::NotSupported;   // không phải thiết bị tty
    }
    make_raw(t);
    ::cfsetispeed(&t, speed);
    ::cfsetospeed(&t, speed);
    t.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB | PARODD | CSTOPB);
#if defined(CRTSCTS)
    t.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
#endif
#if defined(IHFLOW) && defined(OHFLOW)   // QNX: bắt tay phần cứng
    t.c_cflag &= ~static_cast<tcflag_t>(IHFLOW | OHFLOW);
#endif
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag |= cfg.data_bits == 5 ? CS5 : cfg.data_bits == 6 ? CS6 : cfg.data_bits == 7 ? CS7 : CS8;
    if (cfg.parity != UartConfig::Parity::None) t.c_cflag |= PARENB;
    if (cfg.parity == UartConfig::Parity::Odd) t.c_cflag |= PARODD;
    if (cfg.stop_bits == 2) t.c_cflag |= CSTOPB;
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    if (::tcsetattr(fd_, TCSANOW, &t) != 0) {
        close();
        return Status::Error;
    }
    ::tcflush(fd_, TCIOFLUSH);
    return Status::Ok;
}

void TermiosUart::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

std::size_t TermiosUart::write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    std::size_t sent = 0;
    while (fd_ >= 0 && sent < len) {
        if (wait_fd(fd_, POLLOUT, timeout_ms) <= 0) break;
        const ssize_t n = ::write(fd_, data + sent, len - sent);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    return sent;
}

std::size_t TermiosUart::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (fd_ < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    if (wait_fd(fd_, POLLIN, timeout_ms) <= 0) return 0;
    const ssize_t n = ::read(fd_, data, len);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

// ----------------------------------------------------------- FileStorage
Status FileStorage::read(std::size_t offset, void* data, std::size_t len) {
    if (offset + len > size_) return Status::OutOfRange;
    std::memset(data, 0xFF, len);   // vùng chưa ghi = trạng thái erased của Flash
    if (FILE* f = std::fopen(path_, "rb")) {
        if (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0) {
            const std::size_t got = std::fread(data, 1, len, f);   // đọc thiếu (file ngắn hơn) = phần còn lại 0xFF
            (void)got;
        }
        std::fclose(f);
    }
    return Status::Ok;
}

Status FileStorage::write(std::size_t offset, const void* data, std::size_t len) {
    if (offset + len > size_) return Status::OutOfRange;
    FILE* f = std::fopen(path_, "r+b");
    if (f == nullptr) {
        if (!core::is_ok(erase())) return Status::Error;
        f = std::fopen(path_, "r+b");
        if (f == nullptr) return Status::Error;
    }
    const bool ok = std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0 && std::fwrite(data, 1, len, f) == len &&
                    std::fflush(f) == 0;
    if (ok) ::fsync(::fileno(f));   // ghi xuống thiết bị trước khi báo xong (an toàn khi mất điện)
    std::fclose(f);
    return ok ? Status::Ok : Status::Error;
}

Status FileStorage::erase() {
    FILE* f = std::fopen(path_, "r+b");
    if (f == nullptr) f = std::fopen(path_, "wb");   // chưa có file: tạo mới
    if (f == nullptr) return Status::Error;
    std::uint8_t blank[256];   // không cấp phát động: ghi từng khối 0xFF
    std::memset(blank, 0xFF, sizeof(blank));
    bool ok = true;
    for (std::size_t done = 0; ok && done < size_;) {
        const std::size_t n = size_ - done < sizeof(blank) ? size_ - done : sizeof(blank);
        ok = std::fwrite(blank, 1, n, f) == n;
        done += n;
    }
    ok = ok && std::fflush(f) == 0;
    if (ok) ::fsync(::fileno(f));
    std::fclose(f);
    return ok ? Status::Ok : Status::Error;
}

}  // namespace hal::host
