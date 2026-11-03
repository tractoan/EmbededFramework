#pragma once
#include <array>

#include "core/compiler.hpp"
#include "core/config.hpp"
#include "core/noncopyable.hpp"
#include "core/status.hpp"
#include "osal/mutex.hpp"

namespace svc {

// Kênh xuất kết quả lệnh. Mỗi giao thức/kênh tự hiện thực write()
// (UART qua CliProtocol, Telnet, BLE, bộ đệm khi test...).
class CommandOutput {
public:
    virtual ~CommandOutput() = default;
    void print(const char* fmt, ...) EP_PRINTF_FMT(2, 3);
    virtual void write(const char* text, std::size_t len) = 0;
};

class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;
    // argv[0] là tên lệnh. Trả 0 nếu thành công.
    virtual int on_command(int argc, char** argv, CommandOutput& out) = 0;
};

struct Command {
    const char* name         = nullptr;
    const char* help         = nullptr;
    ICommandHandler* handler = nullptr;
};

// Bảng lệnh vận hành/chẩn đoán dùng chung. KHÔNG gắn với cổng truyền thông nào:
// giao thức văn bản (comm::CliProtocol) hoặc bất kỳ giao thức nào khác đều có thể
// gọi execute(). Handler chạy trên thread của kênh gọi -> chỉ đọc dữ liệu thread-safe
// hoặc publish event.
class CommandService : core::NonCopyable {
public:
    core::Status register_command(const Command& cmd);

    // Tách tham số (in-place) và thực thi một dòng lệnh.
    int execute(char* line, CommandOutput& out);
    std::size_t count() const;

private:
    bool find(const char* name, Command& out) const;
    void print_help(CommandOutput& out) const;

    std::array<Command, core::cfg::kMaxCommands> cmds_{};
    std::size_t count_ = 0;
    mutable osal::Mutex mtx_;
};

}  // namespace svc
