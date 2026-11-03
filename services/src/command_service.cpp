#include "services/command_service.hpp"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace svc {

// ---------------------------------------------------------- CommandOutput
void CommandOutput::print(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    write(buf, static_cast<std::size_t>(n) < sizeof(buf) ? static_cast<std::size_t>(n) : sizeof(buf) - 1);
}

void CommandOutput::write(const char* text, std::size_t len) {
    if (uart_ != nullptr) uart_->write(reinterpret_cast<const std::uint8_t*>(text), len, 100);
}

// --------------------------------------------------------- CommandService
void CommandService::init(hal::IUart* uart, const Config& cfg) {
    uart_ = uart;
    cfg_  = cfg;
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
    char* save = nullptr;
    for (char* tok = strtok_r(line, " \t", &save); tok != nullptr && argc < static_cast<int>(core::cfg::kCliMaxArgs);
         tok = strtok_r(nullptr, " \t", &save)) {
        argv[argc++] = tok;
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

core::Status CommandService::start() {
    if (uart_ == nullptr) return core::Status::NotSupported;
    running_ = true;
    return thread_.start(osal::ThreadConfig{"cli", osal::Priority::Low, 3072, nullptr},
                         &osal::Thread::invoke<CommandService, &CommandService::run>, this);
}

void CommandService::stop() {
    running_ = false;
    thread_.join();
}

void CommandService::run() {
    CommandOutput out(uart_);
    out.print("%s", cfg_.prompt);
    while (running_) {
        std::uint8_t ch = 0;
        if (uart_->read(&ch, 1, 100) == 0) continue;

        if (ch == '\r' || ch == '\n') {
            if (cfg_.echo) out.write("\r\n", 2);
            if (len_ > 0) {
                line_[len_] = '\0';
                execute(line_, out);
                len_ = 0;
            }
            out.print("%s", cfg_.prompt);
        } else if (ch == 0x08 || ch == 0x7F) {   // backspace
            if (len_ > 0) {
                --len_;
                if (cfg_.echo) out.write("\b \b", 3);
            }
        } else if (std::isprint(ch) && len_ < sizeof(line_) - 1) {
            line_[len_++] = static_cast<char>(ch);
            if (cfg_.echo) out.write(reinterpret_cast<const char*>(&ch), 1);
        }
    }
}

}  // namespace svc
