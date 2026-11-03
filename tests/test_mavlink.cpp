// MAVLink: codec so với khung sinh bởi pymavlink (golden), tách khung bền vững, giao thức tham số/lệnh.
#include "comm/cli_protocol.hpp"
#include "comm/link_manager.hpp"
#include "mavlink/mavlink_protocol.hpp"
#include "mocks.hpp"
#include "test_framework.hpp"

namespace {

bool std_lookup(std::uint32_t id, std::uint8_t& extra, const void*) { return mavlink::MavlinkProtocol::standard_crc_extra(id, extra); }

std::vector<mavlink::Message> parse_all(const std::vector<std::uint8_t>& bytes) {
    mavlink::Parser p(&std_lookup, nullptr);
    std::vector<mavlink::Message> out;
    p.feed(bytes.data(), bytes.size(), [&](const mavlink::Message& m) { out.push_back(m); });
    return out;
}

template <typename M>
std::vector<std::uint8_t> frame(const M& m, std::uint8_t seq, std::uint8_t sys, std::uint8_t comp) {
    std::uint8_t payload[M::kLen]{};
    m.pack(payload);
    std::uint8_t out[mavlink::kMaxFrame];
    const std::size_t n = mavlink::encode(out, sizeof(out), seq, sys, comp, M::kId, M::kCrcExtra, payload, M::kLen);
    return std::vector<std::uint8_t>(out, out + n);
}

// Dialect thử: ghi lại bản tin/lệnh, đẩy event 0x3001 thành STATUSTEXT
struct TestDialect final : mavlink::IDialect {
    void on_event(const svc::Event& e, mavlink::Sender& tx) override {
        if (e.id != 0x3001) return;
        mavlink::StatusText t;
        std::strcpy(t.text, "hello");
        tx.send(t);
    }
    void on_message(const mavlink::Message& m, comm::Services&, mavlink::Sender&) override { last_msg = m.msgid; }
    bool crc_extra(std::uint32_t id, std::uint8_t& out) const override {
        if (id != 62003) return false;
        out = 2;
        return true;
    }
    std::uint8_t on_command(const mavlink::CommandLong& c, comm::Services&, mavlink::Sender&) override {
        last_cmd = c.command;
        return mavlink::MAV_RESULT_ACCEPTED;
    }
    std::atomic<std::uint32_t> last_msg{0};
    std::atomic<std::uint16_t> last_cmd{0};
};

}  // namespace

TEST(mavlink_encode_matches_pymavlink) {
    mavlink::Heartbeat hb;
    hb.type = 18; hb.autopilot = 8; hb.system_status = 4;
    const std::vector<std::uint8_t> golden = {0xFD, 0x09, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x12, 0x08, 0x00, 0x04, 0x03, 0x16, 0xD2};
    CHECK(frame(hb, 0, 1, 1) == golden);

    // COMMAND_LONG có byte 0 ở cuối -> bị cắt (len 32), giống pymavlink
    const std::vector<std::uint8_t> cmd = {0xFD, 0x20, 0x00, 0x00, 0x03, 0xFF, 0xBE, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF5, 0x00, 0x01, 0x01,
                                           0xF0, 0x0A};
    mavlink::CommandLong c;
    c.target_system = 1; c.target_component = 1; c.command = 245; c.param[0] = 1.0f;
    CHECK(frame(c, 3, 255, 190) == cmd);

    const auto msgs = parse_all(cmd);
    CHECK_EQ(msgs.size(), 1u);
    if (msgs.size() == 1) {
        const auto back = mavlink::CommandLong::unpack(msgs[0].payload);   // payload đã mở rộng 0
        CHECK_EQ(back.command, 245);
        CHECK(back.param[0] == 1.0f && back.confirmation == 0);
        CHECK_EQ(msgs[0].sysid, 255);
        CHECK_EQ(msgs[0].seq, 3);
    }
}

TEST(mavlink_parser_resync_and_errors) {
    mavlink::Heartbeat hb;
    auto good = frame(hb, 1, 1, 1);
    auto bad  = good;
    bad[12] ^= 0x40;   // hỏng payload -> sai CRC

    std::vector<std::uint8_t> stream = {0x00, 0xFD, 0x55, 0xAA};   // rác (có cả byte STX giả)
    stream.insert(stream.end(), bad.begin(), bad.end());
    stream.insert(stream.end(), good.begin(), good.end());

    // khung v1 (STX 0xFE): heartbeat, CRC tính trên header v1
    const std::uint8_t v1_body[] = {0x09, 0x05, 0x02, 0x03, 0x00, 0, 0, 0, 0, 6, 8, 0, 4, 3};
    std::uint16_t crc = utils::crc16_mcrf4xx(v1_body, sizeof(v1_body));
    const std::uint8_t extra = 50;
    crc = utils::crc16_mcrf4xx(&extra, 1, crc);
    stream.push_back(0xFE);
    stream.insert(stream.end(), v1_body, v1_body + sizeof(v1_body));
    stream.push_back(static_cast<std::uint8_t>(crc));
    stream.push_back(static_cast<std::uint8_t>(crc >> 8));

    // khung có chữ ký (13 byte sau CRC) -> nhận, bỏ qua chữ ký
    auto signed_frame = good;
    signed_frame[2] = mavlink::kIncompatSigned;
    std::uint16_t c2 = utils::crc16_mcrf4xx(signed_frame.data() + 1, signed_frame.size() - 3);
    c2 = utils::crc16_mcrf4xx(&extra, 1, c2);
    signed_frame[signed_frame.size() - 2] = static_cast<std::uint8_t>(c2);
    signed_frame[signed_frame.size() - 1] = static_cast<std::uint8_t>(c2 >> 8);
    signed_frame.insert(signed_frame.end(), 13, 0x11);
    stream.insert(stream.end(), signed_frame.begin(), signed_frame.end());

    // Khung hợp lệ bị "che" bởi STX giả: header giả nuốt phần đầu khung thật -> phải quét lại
    const auto hidden = frame(hb, 9, 4, 4);
    stream.push_back(0xFD);
    stream.push_back(0x05);   // len giả = 5 (header giả bị loại vì cờ incompat sai)
    stream.insert(stream.end(), hidden.begin(), hidden.end());

    mavlink::Parser p(&std_lookup, nullptr);
    std::vector<mavlink::Message> got;
    p.feed(stream.data(), stream.size(), [&](const mavlink::Message& m) { got.push_back(m); });
    CHECK_EQ(got.size(), 4u);
    CHECK(p.crc_errors() >= 1u);
    if (got.size() == 4) {
        CHECK_EQ(got[0].seq, 1);
        CHECK_EQ(got[1].version, 1);
        CHECK_EQ(got[1].sysid, 2);
        CHECK_EQ(mavlink::Heartbeat::unpack(got[1].payload).type, 6);
        CHECK_EQ(got[3].sysid, 4);
        CHECK_EQ(got[3].seq, 9);
    }
}

TEST(mavlink_protocol_params_commands_and_events) {
    svc::EventBus bus;
    svc::ParamStore params;
    mock::Storage nv;
    params.init(&nv, &bus);
    const svc::ParamDef defs[] = {{0x3101, "imu.rate_hz", 10, 1, 100, "Hz"}, {0x3102, "x.long_name_here_ok", 5, 0, 9, ""}};
    params.register_params(defs, 2);
    svc::CommandService commands;
    comm::Services services{bus, params, commands};

    TestDialect dialect;
    mavlink::MavlinkProtocol mav(dialect);
    mock::ScriptedStream io;
    CHECK(core::is_ok(mav.attach(io, services)));

    auto rx = [&](const std::vector<std::uint8_t>& f) { mav.on_rx(f.data(), f.size()); };
    auto sent = [&]() { return parse_all(io.tx()); };

    // Heartbeat định kỳ
    mav.poll(1000);
    auto out = sent();
    CHECK(out.size() == 1 && out[0].msgid == mavlink::Heartbeat::kId && out[0].sysid == 1);

    // GCS heartbeat -> connected
    mavlink::Heartbeat gcs_hb;
    gcs_hb.type = mavlink::MAV_TYPE_GCS;
    rx(frame(gcs_hb, 0, 255, 190));
    CHECK(mav.gcs_connected());

    // PARAM_REQUEST_LIST -> 2 PARAM_VALUE (tên dài bị cắt 16 ký tự)
    io.clear_tx();
    std::uint8_t req[2] = {1, 1};
    std::uint8_t f[64];
    std::size_t n = mavlink::encode(f, sizeof(f), 1, 255, 190, mavlink::ParamRequestList::kId, mavlink::ParamRequestList::kCrcExtra, req, 2);
    rx(std::vector<std::uint8_t>(f, f + n));
    mav.poll(1010);
    out = sent();
    CHECK_EQ(out.size(), 2u);
    if (out.size() == 2) {
        CHECK(std::strncmp(reinterpret_cast<const char*>(out[0].payload + 8), "imu.rate_hz", 16) == 0);
        CHECK(std::strncmp(reinterpret_cast<const char*>(out[1].payload + 8), "x.long_name_here", 16) == 0);
        CHECK(mavlink::wire::get_f32(out[0].payload, 0) == 10.0f);
        CHECK_EQ(mavlink::wire::get_u16(out[0].payload, 4), 2);
    }

    // PARAM_SET (trong miền / ngoài miền) -> luôn trả PARAM_VALUE với giá trị hiện tại
    std::uint8_t ps[mavlink::ParamSet::kLen]{};
    mavlink::wire::put_f32(ps, 0, 25.0f);
    ps[4] = 1; ps[5] = 1;
    std::memcpy(ps + 6, "imu.rate_hz", 11);
    ps[22] = mavlink::MAV_PARAM_TYPE_INT32;
    io.clear_tx();
    n = mavlink::encode(f, sizeof(f), 2, 255, 190, mavlink::ParamSet::kId, mavlink::ParamSet::kCrcExtra, ps, sizeof(ps));
    rx(std::vector<std::uint8_t>(f, f + n));
    CHECK_EQ(params.get_or(0x3101, 0), 25);
    mavlink::wire::put_f32(ps, 0, 1000.0f);
    n = mavlink::encode(f, sizeof(f), 3, 255, 190, mavlink::ParamSet::kId, mavlink::ParamSet::kCrcExtra, ps, sizeof(ps));
    rx(std::vector<std::uint8_t>(f, f + n));
    CHECK_EQ(params.get_or(0x3101, 0), 25);
    out = sent();
    CHECK(out.size() == 2 && mavlink::wire::get_f32(out[1].payload, 0) == 25.0f);

    // COMMAND_LONG: PREFLIGHT_STORAGE (giao thức xử lý) + lệnh khác (chuyển cho dialect) -> COMMAND_ACK
    io.clear_tx();
    mavlink::CommandLong c;
    c.target_system = 1; c.command = mavlink::MAV_CMD_PREFLIGHT_STORAGE; c.param[0] = 1;
    rx(frame(c, 4, 255, 190));
    c.command = mavlink::MAV_CMD_DO_SET_RELAY;
    rx(frame(c, 5, 255, 190));
    c.target_system = 9;   // không phải gửi cho mình -> bỏ qua
    rx(frame(c, 6, 255, 190));
    out = sent();
    CHECK_EQ(out.size(), 2u);
    if (out.size() == 2) {
        CHECK_EQ(mavlink::wire::get_u16(out[0].payload, 0), mavlink::MAV_CMD_PREFLIGHT_STORAGE);
        CHECK_EQ(out[0].payload[2], mavlink::MAV_RESULT_ACCEPTED);
        CHECK_EQ(out[1].payload[8], 255);   // ACK gửi về đúng GCS
    }
    CHECK_EQ(dialect.last_cmd.load(), mavlink::MAV_CMD_DO_SET_RELAY);
    CHECK(nv.mem[0] != 0xFF);   // đã ghi NV (save)

    // Bản tin riêng của dialect (CRC_EXTRA do dialect cung cấp)
    std::uint8_t os[10] = {0x00, 0x0C, 0, 0, 0x00, 0x04, 0, 0, 1, 1};
    n = mavlink::encode(f, sizeof(f), 7, 255, 190, 62003, 2, os, sizeof(os));
    rx(std::vector<std::uint8_t>(f, f + n));
    CHECK_EQ(dialect.last_msg.load(), 62003u);

    // Event miền -> bản tin (qua dialect), probe nhận dạng khung hợp lệ
    io.clear_tx();
    bus.publish(svc::Event::make(0x3001));
    out = sent();
    CHECK(out.size() == 1 && out[0].msgid == mavlink::StatusText::kId);
    const auto hb_frame = frame(gcs_hb, 0, 255, 190);
    CHECK(mav.probe(hb_frame.data(), hb_frame.size()));
    CHECK(!mav.probe(hb_frame.data(), hb_frame.size() - 1));

    mav.detach();
    io.clear_tx();
    bus.publish(svc::Event::make(0x3001));
    CHECK(io.tx().empty());
}

TEST(mavlink_auto_detect_on_link) {
    svc::EventBus bus;
    svc::ParamStore params;
    params.init(nullptr, &bus);
    svc::CommandService commands;
    comm::Services services{bus, params, commands};
    comm::LinkManager links;
    links.init(services);

    TestDialect dialect;
    mavlink::MavlinkProtocol mav(dialect);
    comm::CliProtocol cli(false, "> ");
    mock::ScriptedStream port;
    CHECK(core::is_ok(links.add_channel("gcs", &port, {&mav, &cli}, comm::proto::kAuto)));
    CHECK(core::is_ok(links.start()));

    mavlink::Heartbeat hb;
    auto f = frame(hb, 0, 255, 190);
    f.insert(f.begin(), {0x13, 0x37});   // rác trước khung
    port.feed(f);
    CHECK(mock::wait_until([&] { return std::string(links.active_protocol(0)) == "mavlink"; }));
    CHECK(mock::wait_until([&] { return mav.gcs_connected(); }));
    links.stop();
}
