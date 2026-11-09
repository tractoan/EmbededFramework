#include "services/command_service.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace svc {

void CommandOutput::print(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    write(buf, static_cast<std::size_t>(n) < sizeof(buf) ? static_cast<std::size_t>(n) : sizeof(buf) - 1);
}

core::Status CommandService::register_command(const Command& cmd) {
    if (cmd.name == nullptr || cmd.handler == nullptr) return core::Status::InvalidArg;
    osal::LockGuard g(mtx_);
    for (std::size_t i = 0; i < count_; ++i) {
        if (std::strcmp(cmds_[i].name, cmd.name) == 0) return core::Status::Busy;   // trùng tên
    }
    if (count_ >= cmds_.size()) return core::Status::NoMemory;
    cmds_[count_++] = cmd;
    return core::Status::Ok;
}

std::size_t CommandService::count() const {
    osal::LockGuard g(mtx_);
    return count_;
}

bool CommandService::find(const char* name, Command& out) const {
    osal::LockGuard g(mtx_);
    for (std::size_t i = 0; i < count_; ++i) {
        if (std::strcmp(cmds_[i].name, name) == 0) {
            out = cmds_[i];
            return true;
        }
    }
    return false;
}

void CommandService::print_help(CommandOutput& out) const {
    osal::LockGuard g(mtx_);
    out.print("  %-8s %s\r\n", "help", "list commands");
    for (std::size_t i = 0; i < count_; ++i) {
        out.print("  %-8s %s\r\n", cmds_[i].name, cmds_[i].help ? cmds_[i].help : "");
    }
}

int CommandService::execute(char* line, CommandOutput& out) {
    char* argv[core::cfg::kCliMaxArgs] = {};
    int argc = 0;
    // Tách token tại chỗ (không dùng strtok_r: không có trong newlib khi build ISO C++ thuần).
    for (char* p = line; *p != '\0' && argc < static_cast<int>(core::cfg::kCliMaxArgs);) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
        if (*p != '\0') *p++ = '\0';
    }
    if (argc == 0) return 0;

    if (std::strcmp(argv[0], "help") == 0) {
        print_help(out);
        return 0;
    }
    Command cmd;
    if (!find(argv[0], cmd)) {
        out.print("unknown command '%s' (try 'help')\r\n", argv[0]);
        return -1;
    }
    return cmd.handler->on_command(argc, argv, out);   // gọi ngoài khóa
}

}  // namespace svc
