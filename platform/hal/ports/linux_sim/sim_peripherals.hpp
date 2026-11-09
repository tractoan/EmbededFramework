#pragma once
// Peripheral mô phỏng trên Linux: cho phép phát triển & test nghiệp vụ không cần phần cứng.
#include <atomic>
#include <chrono>
#include <mutex>

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

// "UART" thứ hai mô phỏng bằng UDP (127.0.0.1:15020, đổi bằng env EP_SIM_AUX_PORT).
// Mỗi datagram nhận được là một khối byte; phản hồi gửi về địa chỉ gửi gần nhất.
// Dùng để thử giao thức nhị phân (Modbus RTU-over-UDP...) bằng công cụ trên PC.
class SimUdpUart final : public IUart {
public:
    // env_port: biến môi trường cho phép đổi cổng (nullptr = không đổi được).
    explicit SimUdpUart(std::uint16_t default_port, const char* env_port = "EP_SIM_AUX_PORT")
        : env_port_(env_port), port_(default_port) {}
    ~SimUdpUart() override { close(); }
    core::Status open(const UartConfig& cfg) override;
    void close() override;
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::uint16_t port() const { return port_; }

private:
    const char* env_port_;
    int fd_ = -1;
    std::uint16_t port_;
    std::mutex peer_mtx_;       // peer ghi bởi thread đọc, dùng bởi mọi thread ghi
    std::uint8_t peer_[16]{};   // sockaddr_in (giữ header không phụ thuộc socket API)
    bool has_peer_ = false;
    std::uint8_t rx_[512]{};
    std::size_t rx_len_ = 0;
    std::size_t rx_pos_ = 0;
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


}  // namespace hal::sim
