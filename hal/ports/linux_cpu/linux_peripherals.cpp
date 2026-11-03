#include "linux_peripherals.hpp"

#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/if_packet.h>
#include <linux/serial.h>
#include <linux/synclink.h>
#include <linux/tty.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifndef ETH_P_ALL
#define ETH_P_ALL 0x0003
#endif

namespace hal::linux_cpu {

using core::Status;

namespace {

void warn(const char* what, const std::string& dev) {
    std::fprintf(stderr, "[HAL] %s %s: %s\n", what, dev.c_str(), std::strerror(errno));
}

bool split(const std::string& spec, std::string& head, std::string& tail) {
    const std::size_t c = spec.find(':');
    if (c == std::string::npos) return false;
    head = spec.substr(0, c);
    tail = spec.substr(c + 1);
    return true;
}

speed_t to_speed(std::uint32_t baud) {
    switch (baud) {
        case 1200: return B1200;     case 2400: return B2400;     case 4800: return B4800;
        case 9600: return B9600;     case 19200: return B19200;   case 38400: return B38400;
        case 57600: return B57600;   case 115200: return B115200; case 230400: return B230400;
        case 460800: return B460800; case 500000: return B500000; case 576000: return B576000;
        case 921600: return B921600; case 1000000: return B1000000; case 1500000: return B1500000;
        case 2000000: return B2000000; case 3000000: return B3000000; case 4000000: return B4000000;
        default: return 0;
    }
}

// Chờ fd sẵn sàng (events) tối đa timeout_ms. true nếu sẵn sàng.
bool wait_fd(int fd, short events, std::uint32_t timeout_ms) {
    pollfd p{fd, events, 0};
    const int wait = timeout_ms > 60000u ? 60000 : static_cast<int>(timeout_ms);
    for (;;) {
        const int r = ::poll(&p, 1, wait);
        if (r < 0 && errno == EINTR) continue;
        return r > 0 && (p.revents & events);
    }
}

}  // namespace

// ============================================================ LinuxSerial
LinuxSerial::LinuxSerial(std::string spec) : spec_(std::move(spec)) {
    path_ = spec_;
    std::string head, tail;
    if (split(spec_, head, tail)) {
        path_       = head;
        rs485_      = tail.rfind("rs485", 0) == 0;
        rs485_echo_ = tail == "rs485-echo";
    }
}

Status LinuxSerial::open(const UartConfig& cfg) {
    close();
    const speed_t sp = to_speed(cfg.baudrate);
    if (sp == 0) return Status::InvalidArg;
    const int fd = ::open(path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        warn("open", path_);
        return Status::NotFound;
    }
    termios t{};
    if (::tcgetattr(fd, &t) != 0) {
        warn("tcgetattr", path_);
        ::close(fd);
        return Status::Error;
    }
    ::cfmakeraw(&t);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    t.c_cflag |= cfg.data_bits == 7 ? CS7 : CS8;
    if (cfg.parity != UartConfig::Parity::None) t.c_cflag |= PARENB | (cfg.parity == UartConfig::Parity::Odd ? PARODD : 0);
    if (cfg.stop_bits == 2) t.c_cflag |= CSTOPB;
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    ::cfsetispeed(&t, sp);
    ::cfsetospeed(&t, sp);
    if (::tcsetattr(fd, TCSANOW, &t) != 0) {
        warn("tcsetattr", path_);
        ::close(fd);
        return Status::Error;
    }
    if (rs485_) {
        serial_rs485 rs{};
        rs.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | (rs485_echo_ ? SER_RS485_RX_DURING_TX : 0);
        if (::ioctl(fd, TIOCSRS485, &rs) != 0) warn("TIOCSRS485 (driver không hỗ trợ RS485?)", path_);
    }
    ::tcflush(fd, TCIOFLUSH);
    fd_ = fd;
    return Status::Ok;
}

void LinuxSerial::close() {
    const int fd = fd_.exchange(-1);
    if (fd >= 0) ::close(fd);
}

std::size_t LinuxSerial::write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    const int fd = fd_.load();
    if (fd < 0) return 0;
    std::size_t done = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (done < len) {
        const ssize_t n = ::write(fd, data + done, len - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR) break;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0 || !wait_fd(fd, POLLOUT, static_cast<std::uint32_t>(left.count()))) break;
    }
    return done;
}

std::size_t LinuxSerial::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    const int fd = fd_.load();
    if (fd < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    ssize_t n = ::read(fd, data, len);
    if (n <= 0 && timeout_ms > 0 && wait_fd(fd, POLLIN, timeout_ms)) n = ::read(fd, data, len);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

// ============================================================== LinuxGpio
LinuxGpio::LinuxGpio(std::string spec, bool output) : spec_(std::move(spec)), output_(output) {}

LinuxGpio::~LinuxGpio() {
    if (line_fd_ >= 0) ::close(line_fd_);
}

Status LinuxGpio::configure(PinMode mode) {
    if (line_fd_ >= 0) {
        ::close(line_fd_);
        line_fd_ = -1;
    }
    // "gpiochip0:17[:low]"
    std::string chip, rest, line, opt;
    if (!split(spec_, chip, rest)) return Status::InvalidArg;
    if (!split(rest, line, opt)) line = rest;
    const std::string dev = chip.rfind("/dev/", 0) == 0 ? chip : "/dev/" + chip;
    const int chip_fd = ::open(dev.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        warn("open", dev);
        return Status::NotFound;
    }
    gpio_v2_line_request req{};
    req.offsets[0] = static_cast<std::uint32_t>(std::atoi(line.c_str()));
    req.num_lines  = 1;
    std::snprintf(req.consumer, sizeof(req.consumer), "ep_bringup");
    const bool out = mode == PinMode::Output || mode == PinMode::OutputOpenDrain;
    req.config.flags = out ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;
    if (mode == PinMode::OutputOpenDrain) req.config.flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
    if (mode == PinMode::InputPullUp) req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    if (mode == PinMode::InputPullDown) req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
    if (opt == "low") req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
    const int r = ::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req);
    ::close(chip_fd);
    if (r < 0) {
        warn("GPIO_V2_GET_LINE_IOCTL", spec_);
        return Status::Error;
    }
    line_fd_ = req.fd;
    output_  = out;
    if (out) write(false);
    return Status::Ok;
}

Status LinuxGpio::write(bool level) {
    if (!output_ || line_fd_ < 0) return Status::NotSupported;
    gpio_v2_line_values v{};
    v.mask = 1;
    v.bits = level ? 1 : 0;
    if (::ioctl(line_fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) < 0) return Status::Error;
    level_ = level;
    return Status::Ok;
}

bool LinuxGpio::read() const {
    if (line_fd_ < 0) return false;
    if (output_) return level_;
    gpio_v2_line_values v{};
    v.mask = 1;
    if (::ioctl(line_fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) return false;
    return (v.bits & 1) != 0;
}

// ============================================================== LinuxHdlc
Status LinuxHdlc::open_synclink(const std::string& path, const HdlcConfig& cfg) {
    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        warn("open", path);
        return Status::NotFound;
    }
    MGSL_PARAMS p{};
    p.mode = MGSL_MODE_HDLC;
    p.loopback = 0;
    p.flags = cfg.clock == HdlcConfig::Clock::External
                  ? static_cast<unsigned short>(HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_TXCPIN)
                  : static_cast<unsigned short>(HDLC_FLAG_RXC_BRG | HDLC_FLAG_TXC_BRG);
    switch (cfg.encoding) {
        case HdlcConfig::Encoding::Nrz:        p.encoding = HDLC_ENCODING_NRZ; break;
        case HdlcConfig::Encoding::Nrzi:       p.encoding = HDLC_ENCODING_NRZI_SPACE; break;
        case HdlcConfig::Encoding::Fm0:        p.encoding = HDLC_ENCODING_BIPHASE_SPACE; break;
        case HdlcConfig::Encoding::Fm1:        p.encoding = HDLC_ENCODING_BIPHASE_MARK; break;
        case HdlcConfig::Encoding::Manchester: p.encoding = HDLC_ENCODING_BIPHASE_LEVEL; break;
    }
    p.clock_speed = cfg.clock == HdlcConfig::Clock::Internal ? cfg.bitrate : 0;
    p.addr_filter = cfg.address ? cfg.address : 0xFF;
    p.crc_type = cfg.crc == HdlcConfig::Crc::None         ? HDLC_CRC_NONE
                 : cfg.crc == HdlcConfig::Crc::Crc16Ccitt ? HDLC_CRC_16_CCITT
                                                          : HDLC_CRC_32_CCITT;
    p.preamble_length = HDLC_PREAMBLE_LENGTH_8BITS;
    p.preamble        = HDLC_PREAMBLE_PATTERN_NONE;
    int ldisc = N_HDLC;
    if (::ioctl(fd, MGSL_IOCSPARAMS, &p) < 0 || ::ioctl(fd, TIOCSETD, &ldisc) < 0 || ::ioctl(fd, MGSL_IOCRXENABLE, 1) < 0) {
        warn("synclink configure", path);
        ::close(fd);
        return Status::Error;
    }
    fd_ = fd;
    return Status::Ok;
}

Status LinuxHdlc::open(const HdlcConfig& cfg) {
    close();
    std::string kind, target;
    if (!split(spec_, kind, target)) return Status::InvalidArg;
    if (kind == "synclink") return open_synclink(target, cfg);
    if (kind == "nhdlc" || kind == "chardev") {
        const int fd = ::open(target.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            warn("open", target);
            return Status::NotFound;
        }
        int ldisc = N_HDLC;
        if (kind == "nhdlc" && ::ioctl(fd, TIOCSETD, &ldisc) < 0) {
            warn("TIOCSETD N_HDLC", target);
            ::close(fd);
            return Status::Error;
        }
        fd_ = fd;
        return Status::Ok;
    }
    if (kind == "packet") {
        const int fd = ::socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ALL));
        if (fd < 0) {
            warn("socket(AF_PACKET) (cần CAP_NET_RAW)", target);
            return Status::Error;
        }
        sockaddr_ll sll{};
        sll.sll_family   = AF_PACKET;
        sll.sll_protocol = htons(ETH_P_ALL);
        sll.sll_ifindex  = static_cast<int>(::if_nametoindex(target.c_str()));
        if (sll.sll_ifindex == 0 || ::bind(fd, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) < 0) {
            warn("bind", target);
            ::close(fd);
            return Status::NotFound;
        }
        fd_ = fd;
        return Status::Ok;
    }
    return Status::InvalidArg;
}

void LinuxHdlc::close() {
    const int fd = fd_.exchange(-1);
    if (fd >= 0) ::close(fd);
}

std::size_t LinuxHdlc::read_frame(std::uint8_t* buf, std::size_t cap, std::uint32_t timeout_ms, HdlcFrameStatus* st) {
    if (st != nullptr) *st = HdlcFrameStatus{};
    const int fd = fd_.load();
    if (fd < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    if (!wait_fd(fd, POLLIN, timeout_ms)) return 0;
    const ssize_t n = ::read(fd, buf, cap);   // N_HDLC / chardev / packet: mỗi read = 1 khung
    if (n < 0) {
        // N_HDLC: khung lớn hơn cap -> EOVERFLOW (khung bị bỏ)
        if (errno == EOVERFLOW && st != nullptr) st->overrun = true;
        return errno == EOVERFLOW ? 1 : 0;   // 1 byte giả để lớp trên đếm lỗi
    }
    return static_cast<std::size_t>(n);
}

Status LinuxHdlc::write_frame(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    const int fd = fd_.load();
    if (fd < 0) return Status::NotSupported;
    if (!wait_fd(fd, POLLOUT, timeout_ms)) return Status::Timeout;
    return ::write(fd, data, len) == static_cast<ssize_t>(len) ? Status::Ok : Status::Error;
}

// ============================================================== StdioUart
std::size_t StdioUart::write(const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    const ssize_t n = ::write(STDOUT_FILENO, data, len);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

std::size_t StdioUart::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (eof_ || !wait_fd(STDIN_FILENO, POLLIN, timeout_ms)) {
        if (eof_) std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    const ssize_t n = ::read(STDIN_FILENO, data, len);
    if (n <= 0) {
        eof_ = true;   // chạy như dịch vụ (stdin đóng): console im lặng
        return 0;
    }
    return static_cast<std::size_t>(n);
}

// ============================================================ FileStorage
Status FileStorage::read(std::size_t offset, void* data, std::size_t len) {
    if (offset + len > size_) return Status::OutOfRange;
    std::memset(data, 0xFF, len);
    if (FILE* f = std::fopen(path_.c_str(), "rb")) {
        if (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0) (void)std::fread(data, 1, len, f);
        std::fclose(f);
    }
    return Status::Ok;
}

Status FileStorage::write(std::size_t offset, const void* data, std::size_t len) {
    if (offset + len > size_) return Status::OutOfRange;
    FILE* f = std::fopen(path_.c_str(), "r+b");
    if (f == nullptr) {
        if (!core::is_ok(erase())) return Status::Error;
        f = std::fopen(path_.c_str(), "r+b");
        if (f == nullptr) return Status::Error;
    }
    const bool ok = std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0 && std::fwrite(data, 1, len, f) == len;
    const bool synced = std::fflush(f) == 0 && ::fsync(::fileno(f)) == 0;   // mất điện ngay sau "param save"
    std::fclose(f);
    return ok && synced ? Status::Ok : Status::Error;
}

Status FileStorage::erase() {
    FILE* f = std::fopen(path_.c_str(), "wb");
    if (f == nullptr) return Status::Error;
    std::vector<std::uint8_t> blank(size_, 0xFF);
    const bool ok = std::fwrite(blank.data(), 1, blank.size(), f) == blank.size();
    std::fclose(f);
    return ok ? Status::Ok : Status::Error;
}

}  // namespace hal::linux_cpu
