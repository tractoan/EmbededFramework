#pragma once
#include <array>
#include <atomic>

#include "core/compiler.hpp"
#include "core/config.hpp"
#include "hal/uart.hpp"
#include "osal/mutex.hpp"
#include "osal/thread.hpp"

namespace svc {

// Kênh xuất kết quả lệnh. Có thể kế thừa để chuyển hướng (test, telnet, BLE...).
class CommandOutput {
public:
    explicit CommandOutput(hal::IUart* uart = nullptr) : uart_(uart) {}
    virtual ~CommandOutput() = default;
    void print(const char* fmt, ...) EP_PRINTF_FMT(2, 3);
    virtual void write(const char* text, std::size_t len);

private:
    hal::IUart* uart_;
};

class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;
    // argv[0] là tên lệnh. Trả 0 nếu thành công.
    virtual int on_command(int argc, char** argv, CommandOutput& out) = 0;
};

struct Command {
    const char* name           = nullptr;
    const char* help           = nullptr;
    ICommandHandler* handler   = nullptr;
};

// Middleware CLI qua UART: module nghiệp vụ đăng ký lệnh riêng để vận hành/chẩn đoán.
// Handler chạy trên thread CLI -> chỉ được đọc dữ liệu thread-safe hoặc publish event.
class CommandService : core::NonCopyable {
public:
    struct Config {
        bool echo          = core::cfg::kCliEcho;
        const char* prompt = "ep> ";
    };

    void init(hal::IUart* uart, const Config& cfg);
    core::Status register_command(const Command& cmd);
    core::Status start();
    void stop();

    // Tách tham số (in-place) và thực thi một dòng lệnh. Public để test / chạy script.
    int execute(char* line, CommandOutput& out);

private:
    void run();
    bool find(const char* name, Command& out) const;
    void print_help(CommandOutput& out) const;

    hal::IUart* uart_ = nullptr;
    Config cfg_{};
    std::array<Command, core::cfg::kMaxCommands> cmds_{};
    std::size_t count_ = 0;
    mutable osal::Mutex mtx_;
    osal::Thread thread_;
    std::atomic<bool> running_{false};
    char line_[core::cfg::kCliLineMax]{};
    std::size_t len_ = 0;
};

}  // namespace svc
