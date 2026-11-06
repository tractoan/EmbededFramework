#pragma once
#include "comm/protocol.hpp"
#include "core/config.hpp"

namespace comm {

// Giao thức dòng lệnh văn bản: soạn dòng (echo, backspace) rồi thực thi qua
// svc::CommandService. Là MỘT giao thức như mọi giao thức khác -> cùng cổng có thể
// chuyển qua lại CLI <-> Modbus <-> MAVLink.
class CliProtocol final : public IProtocol {
public:
    explicit CliProtocol(bool echo = core::cfg::kCliEcho, const char* prompt = "ep> ") : echo_(echo), prompt_(prompt) {}

    ProtocolId id() const override { return proto::kCli; }
    const char* name() const override { return "cli"; }

    core::Status attach(IByteStream& io, Services& services) override;
    void detach() override;
    void on_rx(const std::uint8_t* data, std::size_t len) override;
    // Nhận dạng: một dòng văn bản in được, kết thúc bằng CR/LF.
    bool probe(const std::uint8_t* data, std::size_t len) const override;

private:
    class StreamOutput final : public svc::CommandOutput {
    public:
        void bind(IByteStream* io) { io_ = io; }
        void write(const char* text, std::size_t len) override {
            if (io_ != nullptr) io_->write_text(text, len);
        }

    private:
        IByteStream* io_ = nullptr;
    };

    bool echo_;
    const char* prompt_;
    svc::CommandService* commands_ = nullptr;
    StreamOutput out_;
    char line_[core::cfg::kCliLineMax]{};
    std::size_t len_ = 0;
    bool last_cr_    = false;
};

}  // namespace comm
