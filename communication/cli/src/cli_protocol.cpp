#include "comm/cli_protocol.hpp"

namespace comm {

namespace {
bool is_text(std::uint8_t c) { return (c >= 0x20 && c <= 0x7E) || c == '\t' || c == 0x08 || c == 0x7F; }
}  // namespace

core::Status CliProtocol::attach(IByteStream& io, Services& services) {
    commands_ = &services.commands;
    out_.bind(&io);
    len_     = 0;
    last_cr_ = false;
    out_.print("%s", prompt_);
    return core::Status::Ok;
}

void CliProtocol::detach() {
    out_.bind(nullptr);
    commands_ = nullptr;
    len_      = 0;
}

void CliProtocol::on_rx(const std::uint8_t* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        const std::uint8_t ch = data[i];
        if (ch == '\r' || ch == '\n') {
            const bool crlf = (ch == '\n' && last_cr_);   // "\r\n" chỉ tính là một lần Enter
            last_cr_ = (ch == '\r');
            if (crlf) continue;
            if (echo_) out_.write("\r\n", 2);
            if (len_ > 0 && commands_ != nullptr) {
                line_[len_] = '\0';
                commands_->execute(line_, out_);
            }
            len_ = 0;
            out_.print("%s", prompt_);
            continue;
        }
        last_cr_ = false;
        if (ch == 0x08 || ch == 0x7F) {
            if (len_ > 0) {
                --len_;
                if (echo_) out_.write("\b \b", 3);
            }
        } else if (ch >= 0x20 && ch <= 0x7E) {
            if (len_ < sizeof(line_) - 1) {
                line_[len_++] = static_cast<char>(ch);
                if (echo_) out_.write(reinterpret_cast<const char*>(&ch), 1);
            }
        } else if (ch != '\t') {
            len_ = 0;   // byte nhị phân (vd. khung của giao thức khác): bỏ dòng đang soạn dở
        }
    }
}

bool CliProtocol::probe(const std::uint8_t* data, std::size_t len) const {
    std::size_t printable = 0;
    for (std::size_t i = 0; i < len; ++i) {
        if (data[i] == '\r' || data[i] == '\n') return printable > 0;
        if (!is_text(data[i])) return false;
        ++printable;
    }
    return false;   // chưa thấy hết dòng
}

}  // namespace comm
