#include "services/serial_stream_port.hpp"

#include "osal/time.hpp"

namespace svc {

core::Status SerialStreamPort::open(const hal::UartConfig& cfg) {
    core::Status st = ports_.set_mode(port_, SerialMode::Stream);
    if (core::is_ok(st) && cfg.baudrate != 0) st = ports_.set_baudrate(port_, cfg.baudrate);
    return st;
}

std::size_t SerialStreamPort::write(const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    if (!streaming()) return 0;              // cổng đã thuộc chế độ khác
    return ports_.write(port_, data, len);   // dãy cổng tự giới hạn thời gian ghi
}

std::size_t SerialStreamPort::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (data == nullptr || len == 0) return 0;
    const std::uint64_t start = osal::now_ms();
    for (;;) {
        if (!streaming()) return 0;   // cổng đã thuộc chế độ khác: trả ngay (không chờ)
        const std::size_t n = ports_.read(port_, data, len);
        if (n > 0) {
            if (tap_.load()) {
                for (std::size_t i = 0; i < n; ++i) {
                    if (!raw_.push(data[i])) {
                        raw_dropped_.fetch_add(static_cast<std::uint32_t>(n - i));
                        break;
                    }
                }
            }
            return n;
        }
        const std::uint64_t waited = osal::now_ms() - start;
        if (waited >= timeout_ms) return 0;
        const std::uint64_t left = timeout_ms - waited;
        osal::sleep_ms(static_cast<std::uint32_t>(left < poll_ms_ ? left : poll_ms_));
    }
}

}  // namespace svc
