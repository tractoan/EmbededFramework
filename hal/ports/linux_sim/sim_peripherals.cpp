#include "sim_peripherals.hpp"

#include <poll.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace hal::sim {

using core::Status;

// ------------------------------------------------------------------ GPIO
Status SimGpio::configure(PinMode mode) {
    mode_ = mode;
    return Status::Ok;
}

Status SimGpio::write(bool level) {
    if (mode_ != PinMode::Output && mode_ != PinMode::OutputOpenDrain) return Status::NotSupported;
    const bool prev = level_.exchange(level);
    if (verbose_ && prev != level) {
        char buf[64];
        const int n = std::snprintf(buf, sizeof(buf), "\033[35m[SIM] GPIO %s -> %s\033[0m\r\n", name_, level ? "ON" : "OFF");
        if (n > 0) (void)!::write(STDOUT_FILENO, buf, static_cast<std::size_t>(n));
    }
    return Status::Ok;
}

// ------------------------------------------------------------------ UART
Status SimConsoleUart::open(const UartConfig& /*cfg*/) { return Status::Ok; }

std::size_t SimConsoleUart::write(const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    const ssize_t n = ::write(STDOUT_FILENO, data, len);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

std::size_t SimConsoleUart::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (eof_) {   // stdin đã đóng: mô phỏng UART im lặng
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    const int wait = timeout_ms > 60000u ? 60000 : static_cast<int>(timeout_ms);
    if (::poll(&pfd, 1, wait) <= 0) return 0;
    const ssize_t n = ::read(STDIN_FILENO, data, len);
    if (n <= 0) {
        eof_ = true;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

// ------------------------------------------------------------------- I2C
double SimI2c::temperature_c() const {
    if (const char* env = std::getenv("EP_SIM_TEMP")) return std::atof(env);
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    return 50.0 + 35.0 * std::sin(2.0 * M_PI * t / 40.0);
}

Status SimI2c::write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    if (addr != kTmp102Addr) return Status::Error;   // NACK
    if (len >= 1) pointer_reg_ = data[0];
    return Status::Ok;
}

Status SimI2c::read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    if (addr != kTmp102Addr) return Status::Error;
    std::memset(data, 0, len);
    if (pointer_reg_ == 0x00 && len >= 2) {   // thanh ghi Temperature: 12-bit, 0.0625°C/LSB
        const auto raw = static_cast<std::int16_t>(std::lround(temperature_c() * 16.0));
        const auto r12 = static_cast<std::uint16_t>(raw) & 0x0FFFu;
        data[0] = static_cast<std::uint8_t>(r12 >> 4);
        data[1] = static_cast<std::uint8_t>((r12 & 0x0Fu) << 4);
    }
    return Status::Ok;
}

Status SimI2c::write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                          std::size_t rx_len, std::uint32_t timeout_ms) {
    const Status st = write(addr, tx, tx_len, timeout_ms);
    return core::is_ok(st) ? read(addr, rx, rx_len, timeout_ms) : st;
}

// --------------------------------------------------------------- Storage
SimFileStorage::SimFileStorage(std::size_t size) : size_(size) {
    const char* env = std::getenv("EP_SIM_NVRAM");
    path_ = env ? env : "ep_nvram.bin";
}

Status SimFileStorage::read(std::size_t offset, void* data, std::size_t len) {
    if (offset + len > size_) return Status::OutOfRange;
    std::memset(data, 0xFF, len);   // vùng chưa ghi = trạng thái erased của Flash
    if (FILE* f = std::fopen(path_.c_str(), "rb")) {
        if (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0) (void)std::fread(data, 1, len, f);
        std::fclose(f);
    }
    return Status::Ok;
}

Status SimFileStorage::write(std::size_t offset, const void* data, std::size_t len) {
    if (offset + len > size_) return Status::OutOfRange;
    FILE* f = std::fopen(path_.c_str(), "r+b");
    if (f == nullptr) {
        if (!core::is_ok(erase())) return Status::Error;
        f = std::fopen(path_.c_str(), "r+b");
        if (f == nullptr) return Status::Error;
    }
    const bool ok = std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0 && std::fwrite(data, 1, len, f) == len;
    std::fclose(f);
    return ok ? Status::Ok : Status::Error;
}

Status SimFileStorage::erase() {
    FILE* f = std::fopen(path_.c_str(), "wb");
    if (f == nullptr) return Status::Error;
    std::vector<std::uint8_t> blank(size_, 0xFF);
    const bool ok = std::fwrite(blank.data(), 1, blank.size(), f) == blank.size();
    std::fclose(f);
    return ok ? Status::Ok : Status::Error;
}

}  // namespace hal::sim
