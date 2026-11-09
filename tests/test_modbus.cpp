#include "mocks.hpp"
#include "modbus/modbus_rtu.hpp"
#include "test_framework.hpp"

namespace {
constexpr msg::EventId kTemp     = 0x3001;
constexpr msg::EventId kAck      = 0x3002;
constexpr svc::ParamId kSetpoint = 0x3101;
constexpr svc::ParamId kPeriod   = 0x3102;

bool extract_i32(const msg::Event& e, std::int32_t& v) { return e.get(v); }
core::Status ack(comm::Services& s, bool on) {
    if (on) s.bus.publish(msg::Event::make(kAck));
    return core::Status::Ok;
}

const modbus::InputRegister kIn[]    = {{0, kTemp, &extract_i32, true}};
const modbus::HoldingRegister kHo[]  = {{0, kSetpoint, true}, {1, kPeriod, false}};
const modbus::Coil kCo[]             = {{5, &ack}};
constexpr modbus::Map kMap           = modbus::make_map(7, kIn, kHo, kCo);
const svc::ParamDef kDefs[] = {
    {kSetpoint, "sp", -50, -400, 400, ""},
    {kPeriod, "period", 1000, 100, 60000, "ms"},
};

struct Fixture {
    msg::EventBus bus;
    svc::ParamStore params;
    svc::CommandService commands;
    comm::Services services{bus, params, commands};
    mock::ScriptedStream io;
    mock::Recorder rec;
    modbus::ModbusRtuProtocol mb{kMap};

    Fixture() {
        params.init(nullptr, &bus);
        params.register_params(kDefs, 2);
        bus.subscribe(kAck, &rec);
        mb.attach(io, services);
    }
    std::vector<std::uint8_t> transact(const std::vector<std::uint8_t>& frame) {
        io.clear_tx();
        mb.on_rx(frame.data(), frame.size());
        return io.tx();
    }
};
}  // namespace

TEST(modbus_read_input_and_holding) {
    Fixture f;
    f.bus.publish(msg::Event::make(kTemp, std::int32_t{-123}));   // -12.3 °C từ "nghiệp vụ"

    CHECK(f.transact(mock::rtu({7, 0x04, 0, 0, 0, 1})) == mock::rtu({7, 0x04, 2, 0xFF, 0x85}));
    CHECK(f.transact(mock::rtu({7, 0x03, 0, 0, 0, 2})) == mock::rtu({7, 0x03, 4, 0xFF, 0xCE, 0x03, 0xE8}));
}

TEST(modbus_write_registers_go_through_param_store) {
    Fixture f;
    const auto req = mock::rtu({7, 0x06, 0, 1, 0x07, 0xD0});   // period = 2000
    CHECK(f.transact(req) == req);                              // echo
    CHECK_EQ(f.params.get_or(kPeriod, 0), 2000);

    CHECK(f.transact(mock::rtu({7, 0x06, 0, 1, 0, 10})) == mock::rtu({7, 0x86, 0x03}));   // ngoài miền
    CHECK(f.transact(mock::rtu({7, 0x06, 0, 9, 0, 1})) == mock::rtu({7, 0x86, 0x02}));    // không có địa chỉ

    // FC16: sp = -100, period = 500
    CHECK(f.transact(mock::rtu({7, 0x10, 0, 0, 0, 2, 4, 0xFF, 0x9C, 0x01, 0xF4})) == mock::rtu({7, 0x10, 0, 0, 0, 2}));
    CHECK_EQ(f.params.get_or(kSetpoint, 0), -100);
    CHECK_EQ(f.params.get_or(kPeriod, 0), 500);
}

TEST(modbus_coil_triggers_action_event) {
    Fixture f;
    const auto req = mock::rtu({7, 0x05, 0, 5, 0xFF, 0x00});
    CHECK(f.transact(req) == req);
    CHECK_EQ(f.rec.count(kAck), 1u);
    CHECK(f.transact(mock::rtu({7, 0x05, 0, 5, 0x12, 0x34})) == mock::rtu({7, 0x85, 0x03}));
}

TEST(modbus_framing_robustness) {
    Fixture f;
    // Khung bị chia nhỏ + có rác phía trước
    auto frame = mock::rtu({7, 0x03, 0, 1, 0, 1});
    std::vector<std::uint8_t> noisy = {0x55, 0xAA};
    noisy.insert(noisy.end(), frame.begin(), frame.end());
    f.io.clear_tx();
    f.mb.on_rx(noisy.data(), 5);
    CHECK(f.io.tx().empty());
    f.mb.on_rx(noisy.data() + 5, noisy.size() - 5);
    CHECK(f.io.tx() == mock::rtu({7, 0x03, 2, 0x03, 0xE8}));

    // Sai CRC -> im lặng; đơn vị khác -> im lặng; broadcast ghi -> không phản hồi nhưng có tác dụng
    auto bad = mock::rtu({7, 0x03, 0, 0, 0, 1});
    bad.back() ^= 0xFF;
    CHECK(f.transact(bad).empty());
    CHECK(f.mb.crc_errors() > 0);
    CHECK(f.transact(mock::rtu({9, 0x03, 0, 0, 0, 1})).empty());
    CHECK(f.transact(mock::rtu({0, 0x06, 0, 0, 0, 42})).empty());
    CHECK_EQ(f.params.get_or(kSetpoint, 0), 42);

    // Function code không hỗ trợ -> exception 01 sau khoảng lặng
    f.io.clear_tx();
    const auto unsupported = mock::rtu({7, 0x2B, 0x0E, 0x01});
    f.mb.on_rx(unsupported.data(), unsupported.size());
    f.mb.poll(osal::now_ms() + 100);
    CHECK(f.io.tx() == mock::rtu({7, 0xAB, 0x01}));
}

TEST(modbus_probe_requires_valid_frame) {
    modbus::ModbusRtuProtocol mb{kMap};
    const auto good  = mock::rtu({7, 0x03, 0, 0, 0, 1});
    auto bad         = good;
    bad[2]           = 0x10;
    const auto other = mock::rtu({3, 0x03, 0, 0, 0, 1});
    CHECK(mb.probe(good.data(), good.size()));
    CHECK(!mb.probe(good.data(), 5));
    CHECK(!mb.probe(bad.data(), bad.size()));
    CHECK(!mb.probe(other.data(), other.size()));
}
