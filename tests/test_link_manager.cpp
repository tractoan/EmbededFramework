// Kiểm tra đổi giao thức lúc chạy trên cùng một kênh: CLI -> Modbus -> auto-detect.
#include "comm/cli_protocol.hpp"
#include "comm/link_manager.hpp"
#include "mocks.hpp"
#include "modbus/modbus_rtu.hpp"
#include "test_framework.hpp"

namespace {
const modbus::HoldingRegister kHo[] = {{0, comm::LinkManager::kParamBase, false}};   // comm.port
const modbus::InputRegister kIn[]   = {{0, 0x3001, nullptr, false}};
const modbus::Coil kCo[]            = {{0, nullptr}};
constexpr modbus::Map kMap          = modbus::Map{1, kIn, 0, kHo, 1, kCo, 0};
}  // namespace

TEST(link_manager_switches_protocol_at_runtime) {
    msg::EventBus bus;
    svc::ParamStore params;
    params.init(nullptr, &bus);
    svc::CommandService commands;
    comm::Services services{bus, params, commands};

    comm::LinkManager links;
    CHECK(core::is_ok(links.init(services)));
    mock::ScriptedStream port;
    comm::CliProtocol cli(false, "> ");
    modbus::ModbusRtuProtocol mb(kMap);
    CHECK(core::is_ok(links.add_channel("port", &port, {&mb, &cli}, comm::proto::kCli)));
    CHECK(links.add_channel("port", &port, {&cli}, comm::proto::kCli) == core::Status::Busy);
    CHECK(params.find("comm.port") != nullptr);
    CHECK(core::is_ok(links.start()));

    // 1) CLI
    CHECK(mock::wait_until([&] { return std::string(links.active_protocol(0)) == "cli"; }));
    port.feed("link\n");
    CHECK(mock::wait_until([&] { return port.tx_text().find("setting=cli") != std::string::npos; }));

    // 2) Đổi sang Modbus bằng chính lệnh CLI trên kênh đó
    port.clear_tx();
    port.feed("link set port modbus\n");
    CHECK(mock::wait_until([&] { return std::string(links.active_protocol(0)) == "modbus"; }));
    CHECK_EQ(params.get_or(comm::LinkManager::kParamBase, 0), comm::proto::kModbusRtu);
    port.clear_tx();
    port.feed(mock::rtu({1, 0x03, 0, 0, 0, 1}));   // đọc lại chính tham số comm.port qua Modbus
    CHECK(mock::wait_until([&] { return port.tx() == mock::rtu({1, 0x03, 2, 0, comm::proto::kModbusRtu}); }));

    // 3) Giao thức không có trên kênh -> giữ nguyên và trả tham số về giá trị cũ
    params.set(comm::LinkManager::kParamBase, comm::proto::kMavlink);
    CHECK(mock::wait_until([&] { return params.get_or(comm::LinkManager::kParamBase, 0) == comm::proto::kModbusRtu; }));
    CHECK(std::string(links.active_protocol(0)) == "modbus");

    // 4) Đổi sang auto qua Modbus (FC06) -> kênh nhận dạng CLI từ dòng văn bản đầu tiên
    port.feed(mock::rtu({1, 0x06, 0, 0, 0, comm::proto::kAuto}));
    CHECK(mock::wait_until([&] { return std::string(links.active_protocol(0)) == "none"; }));
    port.clear_tx();
    port.feed(std::vector<std::uint8_t>{0x00, 0xFF});   // rác trước khung
    port.feed("help\n");
    CHECK(mock::wait_until([&] { return std::string(links.active_protocol(0)) == "cli"; }));
    CHECK(mock::wait_until([&] { return port.tx_text().find("link") != std::string::npos; }));

    links.stop();
    CHECK(std::string(links.active_protocol(0)) == "none");
}
