#pragma once
// Ngoại vi HAL dùng chung cho mọi OS POSIX (Linux, QNX, macOS…): chỉ dùng API chuẩn POSIX.
//   StdioUart     : console = stdin/stdout của tiến trình
//   TermiosUart   : thiết bị serial (/dev/ttyXXX trên Linux, /dev/serN trên QNX) – termios, raw mode
//   FileStorage   : file thường (hoặc file EEPROM của kernel Linux)
// Phần riêng của từng OS (I2C, GPIO) nằm ở common/linux, common/qnx. Không cấp phát heap sau init.
#include "hal/board.hpp"

namespace hal::host {

class StdioUart final : public IUart {
public:
    core::Status open(const UartConfig& cfg) override;
    void close() override {}
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    bool eof_ = false;
};

class TermiosUart final : public IUart {
public:
    explicit TermiosUart(const char* path) : path_(path) {}
    ~TermiosUart() override { close(); }
    core::Status open(const UartConfig& cfg) override;
    void close() override;
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    bool is_open() const { return fd_ >= 0; }
    const char* path() const { return path_; }

private:
    const char* path_;
    int fd_ = -1;
};

class FileStorage final : public INvStorage {
public:
    FileStorage(const char* path, std::size_t size) : path_(path), size_(size) {}
    std::size_t size() const override { return size_; }
    core::Status read(std::size_t offset, void* data, std::size_t len) override;
    core::Status write(std::size_t offset, const void* data, std::size_t len) override;
    core::Status erase() override;

private:
    const char* path_;
    std::size_t size_;
};

// Tiện ích cấu hình: biến môi trường (nếu có) ghi đè giá trị mặc định lúc build.
const char* env_or(const char* name, const char* fallback);
unsigned env_or(const char* name, unsigned fallback);

}  // namespace hal::host
