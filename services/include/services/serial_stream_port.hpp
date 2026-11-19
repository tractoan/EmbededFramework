#pragma once
// SerialStreamPort: hal::IUart trên MỘT cổng chế độ Stream của ISerialPorts (SerialPortBank).
// Cho phép dùng cổng do dãy cổng quản lý (đổi Off/Loopback/Stream lúc chạy) với mọi thứ cần IUart –
// điển hình: svc::StreamSensorTask (vd. GNSS: NmeaFixParser -> Topic<GnssFix>).
//   - read(): rút byte từ ring buffer của cổng, chờ tối đa timeout (thăm dò mỗi poll_ms).
//     Cổng KHÔNG ở chế độ Stream (vd. đã chuyển sang Loopback) -> trả 0 NGAY: StreamSensorTask hiểu là
//     "UART không chờ được" và nghỉ phần còn lại của timeout (không quay bận, không tranh byte với loopback).
//   - write(): ghi thẳng ra cổng (chỉ khi Stream). Có thể chặn tới ~100 ms – gọi từ thread được phép chậm.
//   - open(): đặt cổng sang Stream + baudrate; close(): không làm gì (dãy cổng sở hữu UART).
//   - tap byte thô (tùy chọn): bản sao mọi byte read() trả về cho MỘT người đọc khác (vd. chuyển tiếp lên GUI).
// read() và read_raw() mỗi hàm chỉ một thread gọi (ring buffer một ghi - một đọc).
#include <atomic>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "hal/uart.hpp"
#include "services/serial_port_bank.hpp"
#include "utils/ring_buffer.hpp"

namespace svc {

class SerialStreamPort final : public hal::IUart, private core::NonCopyable {
public:
    static constexpr std::size_t kRawTap = 4096;

    SerialStreamPort(ISerialPorts& ports, std::size_t port, std::uint32_t poll_ms = 2)
        : ports_(ports), port_(port), poll_ms_(poll_ms == 0 ? 1 : poll_ms) {}

    core::Status open(const hal::UartConfig& cfg) override;
    void close() override {}
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

    std::size_t port() const { return port_; }
    bool streaming() const { return ports_.stats(port_).mode == SerialMode::Stream; }

    void set_raw_tap(bool enabled) { tap_ = enabled; }
    bool raw_tap() const { return tap_.load(); }
    std::size_t read_raw(std::uint8_t* data, std::size_t max) { return raw_.pop(data, max); }
    std::uint32_t raw_dropped() const { return raw_dropped_.load(); }   // byte thô bỏ do tap đầy

private:
    ISerialPorts& ports_;
    std::size_t port_;
    std::uint32_t poll_ms_;
    std::atomic<bool> tap_{false};
    std::atomic<std::uint32_t> raw_dropped_{0};
    utils::RingBuffer<std::uint8_t, kRawTap> raw_;
};

}  // namespace svc
