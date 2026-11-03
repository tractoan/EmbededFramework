#include <cstring>

#include "comm/cli_protocol.hpp"
#include "mocks.hpp"
#include "test_framework.hpp"

namespace {
class Echo final : public svc::ICommandHandler {
public:
    int on_command(int argc, char** argv, svc::CommandOutput& out) override {
        out.print("argc=%d last=%s\r\n", argc, argv[argc - 1]);
        return 0;
    }
};

std::size_t feed(comm::IProtocol& p, const char* text) {
    p.on_rx(reinterpret_cast<const std::uint8_t*>(text), std::strlen(text));
    return std::strlen(text);
}
}  // namespace

TEST(cli_protocol_line_editing_and_dispatch) {
    svc::EventBus bus;
    svc::ParamStore params;
    svc::CommandService commands;
    comm::Services services{bus, params, commands};
    Echo echo;
    commands.register_command({"echo", "test", &echo});

    mock::ScriptedStream io;
    comm::CliProtocol cli(false, "> ");
    CHECK(core::is_ok(cli.attach(io, services)));
    CHECK(io.tx_text() == "> ");

    io.clear_tx();
    feed(cli, "ech");
    feed(cli, "o a bXc\b\b\r\n");   // tách thành nhiều lần nhận + backspace + CRLF
    CHECK(io.tx_text() == "argc=3 last=b\r\n> ");

    // Byte nhị phân lẫn vào (vd. khung Modbus gửi nhầm) không được làm hỏng lệnh kế tiếp
    io.clear_tx();
    const std::uint8_t junk[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x31, 0xCA};
    cli.on_rx(junk, sizeof(junk));
    feed(cli, "echo z\n");
    CHECK(io.tx_text() == "argc=2 last=z\r\n> ");

    io.clear_tx();
    feed(cli, "nope\n");
    CHECK(io.tx_text().find("unknown command 'nope'") != std::string::npos);

    cli.detach();
    io.clear_tx();
    feed(cli, "echo x\n");   // đã detach: không xuất gì
    CHECK(io.tx().empty());
}

TEST(cli_protocol_probe) {
    comm::CliProtocol cli;
    const std::uint8_t line[]    = {'h', 'e', 'l', 'p', '\r'};
    const std::uint8_t partial[] = {'h', 'e', 'l'};
    const std::uint8_t binary[]  = {0x01, 0x03, 0x00, '\n'};
    const std::uint8_t empty[]   = {'\n'};
    CHECK(cli.probe(line, sizeof(line)));
    CHECK(!cli.probe(partial, sizeof(partial)));
    CHECK(!cli.probe(binary, sizeof(binary)));
    CHECK(!cli.probe(empty, sizeof(empty)));
}
