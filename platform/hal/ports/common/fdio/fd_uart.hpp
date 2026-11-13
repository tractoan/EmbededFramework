#pragma once
// UART qua file descriptor KHÔNG CHẶN, chỉ dùng open/read/write/close + O_NONBLOCK (không cần poll/termios):
// hợp với môi trường có lớp file descriptor tối giản như HAL của Nios II (/dev/jtag_uart, /dev/uart_0 –
// driver HAL nhận byte bằng ngắt vào buffer riêng nên đọc thăm dò không mất dữ liệu).
// Timeout: thử đọc/ghi, chưa được thì chờ 1 ms (hal::wait_ms – nhường CPU khi RTOS đã chạy; chưa có thì
// usleep) cho tới hết thời gian. Cấu hình baud do phần cứng/BSP quyết định (open() bỏ qua UartConfig).
#include "hal/uart.hpp"

namespace hal::fdio {

class FdUart final : public IUart {
public:
    explicit FdUart(const char* path) : path_(path) {}
    ~FdUart() override { close(); }

    core::Status open(const UartConfig& cfg) override;   // NotFound nếu không mở được thiết bị
    void close() override;
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

    bool is_open() const { return fd_ >= 0; }
    const char* path() const { return path_; }

private:
    const char* path_;
    int fd_ = -1;
};

}  // namespace hal::fdio
