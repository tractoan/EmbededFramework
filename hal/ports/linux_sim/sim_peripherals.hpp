#pragma once
// Peripheral mô phỏng trên Linux: cho phép phát triển & test nghiệp vụ không cần phần cứng.
#include <atomic>
#include <chrono>
#include <string>

#include "hal/board.hpp"

namespace hal::sim {

class SimGpio final : public IGpio {
public:
    SimGpio(const char* name, bool verbose) : name_(name), verbose_(verbose) {}
    core::Status configure(PinMode mode) override;
    core::Status write(bool level) override;
    bool read() const override { return level_.load(); }

private:
    const char* name_;
    bool verbose_;
    PinMode mode_ = PinMode::Input;
    std::atomic<bool> level_{false};
};

// Console UART = stdin/stdout.
class SimConsoleUart final : public IUart {
public:
    core::Status open(const UartConfig& cfg) override;
    void close() override {}
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    bool eof_ = false;
};

// Bus I2C có 1 thiết bị mô phỏng: cảm biến nhiệt TMP102 @0x48.
// Nhiệt độ dao động hình sin 15..85°C chu kỳ 40s, hoặc cố định qua env EP_SIM_TEMP.
class SimI2c final : public II2c {
public:
    static constexpr std::uint8_t kTmp102Addr = 0x48;
    SimI2c() : start_(std::chrono::steady_clock::now()) {}
    core::Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    core::Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    core::Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                            std::size_t rx_len, std::uint32_t timeout_ms) override;

private:
    double temperature_c() const;
    std::uint8_t pointer_reg_ = 0;
    std::chrono::steady_clock::time_point start_;
};

// "Flash" mô phỏng bằng file (mặc định ./ep_nvram.bin, đổi bằng env EP_SIM_NVRAM).
class SimFileStorage final : public INvStorage {
public:
    explicit SimFileStorage(std::size_t size);
    std::size_t size() const override { return size_; }
    core::Status read(std::size_t offset, void* data, std::size_t len) override;
    core::Status write(std::size_t offset, const void* data, std::size_t len) override;
    core::Status erase() override;

private:
    std::string path_;
    std::size_t size_;
};

}  // namespace hal::sim
