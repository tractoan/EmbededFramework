#pragma once
#include <cstddef>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "hal/uart.hpp"
#include "osal/mutex.hpp"
#include "osal/time.hpp"

namespace comm {

// Tầng vận chuyển: giao thức chỉ thấy một luồng byte, không biết bên dưới là
// UART, UDP, USB CDC, BLE hay bộ đệm trong RAM.
class IByteStream {
public:
    virtual ~IByteStream() = default;
    virtual std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) = 0;
    // Trả về số byte đọc được (0 khi hết timeout).
    virtual std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) = 0;

    std::size_t write_text(const char* text, std::size_t len) {
        return write(reinterpret_cast<const std::uint8_t*>(text), len, 100);
    }
};

// Adapter: hal::IUart -> IByteStream
class UartStream final : public IByteStream {
public:
    void attach(hal::IUart* uart) { uart_ = uart; }
    bool valid() const { return uart_ != nullptr; }

    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        return uart_ ? uart_->write(data, len, timeout_ms) : 0;
    }
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        if (uart_ == nullptr) {
            osal::sleep_ms(timeout_ms);
            return 0;
        }
        return uart_->read(data, len, timeout_ms);
    }

private:
    hal::IUart* uart_ = nullptr;
};

// Bọc một stream để nhiều thread cùng ghi an toàn (mỗi lần write là nguyên khối).
// Đọc chỉ do thread RX của kênh thực hiện nên không cần khóa.
class SerializedStream final : public IByteStream, private core::NonCopyable {
public:
    void bind(IByteStream* inner) { inner_ = inner; }

    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        if (inner_ == nullptr) return 0;
        osal::LockGuard g(mtx_);
        return inner_->write(data, len, timeout_ms);
    }
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        return inner_ ? inner_->read(data, len, timeout_ms) : 0;
    }

private:
    IByteStream* inner_ = nullptr;
    osal::Mutex mtx_;
};

}  // namespace comm
