// Module bring-up (chỉ build với EP_PRODUCT=hw_bringup: event payload 128 byte):
// phát hiện máy thu GNSS trong 5 s, chuyển cổng sang loopback, chuyển tiếp dữ liệu/lệnh,
// đặt output, báo IMU; dialect MAVLink ánh xạ Event <-> bản tin.
#include <cmath>

#include "bringup_events.hpp"
#include "bringup_messages.hpp"
#include "imu_bringup/imu_bringup.hpp"
#include "io_bringup/io_bringup.hpp"
#include "mavlink_dialect.hpp"
#include "mocks.hpp"
#include "osal/time.hpp"
#include "serial_bringup/serial_bringup.hpp"
#include "test_framework.hpp"

namespace {
namespace bu = app::bringup;

// Dãy cổng giả: điều khiển hoàn toàn bởi test (không thread).
struct FakePorts final : svc::ISerialPorts {
    std::size_t n = 12;
    svc::SerialPortStats st[12]{};
    std::string rx[12], tx[12];
    std::uint32_t period = 0, timeout = 0;
    bool started = false;

    std::size_t count() const override { return n; }
    core::Status set_mode(std::size_t p, svc::SerialMode m) override { st[p].mode = m; return core::Status::Ok; }
    svc::SerialPortStats stats(std::size_t p) const override { return st[p]; }
    void reset_counters(std::size_t p) override { st[p].pass = st[p].fail = 0; }
    std::size_t write(std::size_t p, const std::uint8_t* d, std::size_t len) override {
        tx[p].append(reinterpret_cast<const char*>(d), len);
        st[p].tx_bytes += static_cast<std::uint32_t>(len);
        return len;
    }
    std::size_t read(std::size_t p, std::uint8_t* d, std::size_t max) override {
        const std::size_t k = std::min(max, rx[p].size());
        std::memcpy(d, rx[p].data(), k);
        rx[p].erase(0, k);
        return k;
    }
    core::Status set_baudrate(std::size_t p, std::uint32_t b) override { st[p].baudrate = b; return core::Status::Ok; }
    void set_test_timing(std::uint32_t p, std::uint32_t t) override { period = p; timeout = t; }
    core::Status start() override { started = true; return core::Status::Ok; }
    void stop() override { started = false; }
    void incoming(std::size_t p, const std::string& s) {
        rx[p] += s;
        st[p].rx_bytes += static_cast<std::uint32_t>(s.size());
    }
};

struct Ctx {
    svc::EventBus bus;
    svc::ParamStore params;
    svc::CommandService cli;
    svc::LedService leds;
    app::Devices devices;
    app::SystemContext ctx{bus, params, cli, leds, devices};
    mock::Recorder rec;
    Ctx() {
        params.init(nullptr, &bus);
        bus.subscribe(svc::evt::kAny, &rec);
    }
    template <typename T>
    bool last(svc::EventId id, T& out) const {
        for (auto it = rec.events.rbegin(); it != rec.events.rend(); ++it) {
            if (it->id == id) return it->get(out);
        }
        return false;
    }
    bool has_text(const char* needle) const {
        for (const auto& e : rec.events) {
            bu::StatusText t{};
            if (e.id == bu::kStatusText && e.get(t) && std::strstr(t.text, needle)) return true;
        }
        return false;
    }
};

struct CaptureOut final : svc::CommandOutput {
    std::string text;
    void write(const char* t, std::size_t n) override { text.append(t, n); }
};

struct TxCapture final : mavlink::Sender {
    struct Sent { std::uint32_t id; std::vector<std::uint8_t> payload; };
    std::vector<Sent> sent;
    bool send_raw(std::uint32_t id, std::uint8_t, const std::uint8_t* p, std::size_t len) override {
        sent.push_back({id, std::vector<std::uint8_t>(p, p + len)});
        return true;
    }
};
}  // namespace

TEST(serial_bringup_gnss_absent_switches_to_loopback) {
    Ctx c;
    FakePorts ports;
    app::SerialBringup m({&ports, 0, 1});
    CHECK(core::is_ok(m.on_init(c.ctx)));
    CHECK(core::is_ok(m.on_start()));
    const std::uint64_t t0 = osal::now_ms();
    CHECK(ports.started && ports.st[1].mode == svc::SerialMode::Stream && ports.st[1].baudrate == 115200);
    CHECK_EQ(ports.period, 200u);

    m.on_tick(t0 + 4900);
    CHECK(m.gnss_state() == bu::GnssState::Detecting);
    m.on_tick(t0 + 5100);   // hết cửa sổ 5 s, không có byte nào
    CHECK(m.gnss_state() == bu::GnssState::Absent);
    CHECK(ports.st[1].mode == svc::SerialMode::Loopback);
    CHECK(c.has_text("port 1 -> loopback"));

    // Lệnh GNSS khi không có máy thu bị bỏ, có thông báo
    bu::GnssTxRequest req{};
    req.len = 3;
    std::memcpy(req.data, "ABC", 3);
    m.on_event(svc::Event::make(bu::kGnssTxRequest, req));
    CHECK(ports.tx[1].empty() && c.has_text("command dropped"));

    // Báo cáo loopback: vai trò + kết quả từng cổng
    ports.st[2].mode = svc::SerialMode::Loopback; ports.st[2].last = svc::LoopResult::Pass; ports.st[2].pass = 7;
    ports.st[3].mode = svc::SerialMode::Loopback; ports.st[3].last = svc::LoopResult::Timeout; ports.st[3].fail = 2;
    ports.st[1].last = svc::LoopResult::Pass;
    m.on_tick(t0 + 10000);
    bu::LoopbackStatus ls{};
    CHECK(c.last(bu::kLoopbackStatus, ls));
    CHECK_EQ(ls.count, 12);
    CHECK(ls.role[0] == static_cast<std::uint8_t>(bu::SerialRole::Gcs));
    CHECK(ls.role[1] == static_cast<std::uint8_t>(bu::SerialRole::Loopback));
    CHECK((ls.ok_mask & 0x6) == 0x6 && (ls.ok_mask & 0x8) == 0);
    CHECK((ls.tested_mask & 0xE) == 0xE && (ls.tested_mask & 0x1) == 0);
    CHECK_EQ(ls.pass[2], 7u);
    CHECK(c.has_text("Serial 3 loopback FAIL (TIMEOUT)"));
    bu::BringupStatus bs{};
    CHECK(c.last(bu::kBringupStatus, bs) && bs.gnss_state == bu::GnssState::Absent && bs.gnss_port == 1);
}

TEST(serial_bringup_gnss_connected_forwards_data_and_commands) {
    Ctx c;
    FakePorts ports;
    app::SerialBringup m({&ports, 0, 1});
    m.on_init(c.ctx);
    m.on_start();
    const std::uint64_t t0 = osal::now_ms();
    ports.incoming(1, std::string(100, 'N'));   // 100 byte NMEA trong 5 s đầu
    m.on_tick(t0 + 1200);
    CHECK(m.gnss_state() == bu::GnssState::Connected);
    CHECK(ports.st[1].mode == svc::SerialMode::Stream);
    CHECK_EQ(c.rec.count(bu::kGnssRxData), 2u);   // 70 + 30
    m.on_tick(t0 + 9000);                          // đã kết nối: không bao giờ chuyển sang loopback
    CHECK(m.gnss_state() == bu::GnssState::Connected);

    bu::GnssTxRequest req{};
    req.baudrate = 38400;
    req.len      = 5;
    std::memcpy(req.data, "CFG\r\n", 5);
    m.on_event(svc::Event::make(bu::kGnssTxRequest, req));
    CHECK(ports.tx[1] == "CFG\r\n" && ports.st[1].baudrate == 38400);

    // CLI "gnss send ..." -> event yêu cầu (thread kênh không ghi trực tiếp)
    char line[] = "gnss send $PCMD,1 2";
    CaptureOut out;
    c.cli.execute(line, out);
    bu::GnssTxRequest q{};
    CHECK(c.last(bu::kGnssTxRequest, q) && std::string(reinterpret_cast<char*>(q.data), q.len) == "$PCMD,1 2\r\n");

    // gnss.forward = 0 -> không chuyển tiếp
    c.params.set(app::params::kGnssForward, 0);
    for (const auto& e : std::vector<svc::Event>(c.rec.events)) m.on_event(e);
    const std::size_t before = c.rec.count(bu::kGnssRxData);
    ports.incoming(1, "XYZ");
    m.on_tick(t0 + 9100);
    CHECK_EQ(c.rec.count(bu::kGnssRxData), before);
}

TEST(io_bringup_outputs_and_inputs) {
    Ctx c;
    mock::Gpio o[20], i[20];
    svc::GpioOutputBank outs;
    svc::GpioInputBank ins;
    for (auto& p : o) outs.add(&p);
    for (auto& p : i) ins.add(&p);
    app::IoBringup m(&ins, &outs);
    CHECK(core::is_ok(m.on_init(c.ctx)));
    m.on_start();
    m.on_tick(1000);
    bu::IoStatus s{};
    CHECK(c.last(bu::kIoStatus, s) && s.input_count == 20 && s.output_count == 20 && s.inputs == 0);

    m.on_event(svc::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{0x00C00, 0x00400}));
    CHECK(o[10].read() && !o[11].read());
    CHECK(c.last(bu::kIoStatus, s) && s.outputs == 0x400u);

    const std::size_t n = c.rec.count(bu::kIoStatus);
    m.on_tick(1020);                     // không đổi, chưa tới chu kỳ báo cáo
    CHECK_EQ(c.rec.count(bu::kIoStatus), n);
    i[19].write(true);                   // input đổi -> báo ngay
    m.on_tick(1040);
    CHECK(c.last(bu::kIoStatus, s) && s.inputs == (1u << 19));

    m.on_event(svc::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{1u << 25, 1u << 25}));   // kênh không có
    CHECK(c.has_text("output set"));
}

TEST(imu_bringup_reports_samples_and_failures) {
    struct FakeImu final : svc::IImu {
        bool fail_start = false, has = false;
        svc::ImuStats st{};
        svc::ImuSample s{};
        const char* format_name() const override { return "ln200"; }
        std::uint8_t type_id() const override { return 1; }
        core::Status start() override { return fail_start ? core::Status::NotFound : core::Status::Ok; }
        void stop() override {}
        bool latest(svc::ImuSample& out) const override { out = s; return has; }
        svc::ImuStats stats() const override { return st; }
    } a, b;
    b.fail_start = true;
    svc::IImu* list[] = {&a, &b};
    Ctx c;
    app::ImuBringup m(list, 2);
    CHECK(core::is_ok(m.on_init(c.ctx)));
    CHECK(core::is_ok(m.on_start()));   // IMU lỗi không làm hỏng khởi động
    CHECK(c.has_text("IMU1 (ln200): start failed"));
    CHECK_EQ(m.tick_period_ms(), 100u);

    const std::uint64_t now = osal::now_ms() + 1000;
    a.has = true;
    a.st.frames = 400; a.st.rate_hz = 400; a.st.last_frame_ms = now - 10;
    a.s.gyro[2] = 0.17f; a.s.raw_len = 2; a.s.raw[0] = 0xAB;
    m.on_tick(now);
    int seen = 0;
    for (const auto& e : c.rec.events) {
        bu::ImuSample ev{};
        if (e.id != bu::kImuSample || !e.get(ev)) continue;
        ++seen;
        if (ev.imu == 0) {
            CHECK(ev.flags == (bu::kImuFresh | bu::kImuEverReceived) && ev.rate_hz == 400 && ev.gyro[2] == 0.17f);
            CHECK(ev.raw_len == 2 && ev.raw[0] == 0xAB && ev.type == 1);
        } else {
            CHECK(ev.flags == bu::kImuStartFailed && std::isnan(ev.temperature));
        }
    }
    CHECK_EQ(seen, 2);
    CHECK(c.has_text("IMU0 (ln200): data OK"));
    a.st.last_frame_ms = 1;   // khung cuối từ rất lâu -> mất dữ liệu
    m.on_tick(now + 100);
    CHECK(c.has_text("IMU0 (ln200): no valid frame"));
}

TEST(bringup_dialect_maps_events_and_messages) {
    product::BringupDialect d;
    TxCapture tx;
    svc::EventBus bus;
    svc::ParamStore params;
    params.init(nullptr, &bus);
    svc::CommandService commands;
    comm::Services services{bus, params, commands};
    mock::Recorder rec;
    bus.subscribe(svc::evt::kAny, &rec);

    bu::ImuSample s{};
    s.imu = 1; s.type = 2; s.frames = 77; s.gyro[0] = 1.5f; s.accel[2] = -9.8f; s.rate_hz = 100; s.raw_len = 3; s.raw[2] = 0x5A;
    d.on_event(svc::Event::make(bu::kImuSample, s), tx);
    bu::GnssData g{};
    g.len = 4;
    std::memcpy(g.data, "$GPG", 4);
    d.on_event(svc::Event::make(bu::kGnssRxData, g), tx);
    bu::LoopbackStatus ls{};
    ls.count = 12; ls.ok_mask = 0x0FFC; ls.pass[11] = 9; ls.role[0] = 1;
    d.on_event(svc::Event::make(bu::kLoopbackStatus, ls), tx);
    CHECK_EQ(tx.sent.size(), 3u);
    if (tx.sent.size() == 3) {
        const auto& p = tx.sent[0].payload;
        CHECK_EQ(tx.sent[0].id, product::msg::ImuData::kId);
        CHECK_EQ(mavlink::wire::get_u32(p.data(), 4), 77u);
        CHECK(mavlink::wire::get_f32(p.data(), 12) == 1.5f && mavlink::wire::get_f32(p.data(), 32) == -9.8f);
        CHECK(p[44] == 1 && p[45] == 2 && p[47] == 3 && p[50] == 0x5A);
        const auto& q = tx.sent[1].payload;   // SERIAL_CONTROL REPLY, device GPS1
        CHECK(tx.sent[1].id == mavlink::SerialControl::kId && q[6] == 1 && q[7] == 1 && q[8] == 4 && q[9] == '$');
        const auto& r = tx.sent[2].payload;
        CHECK(mavlink::wire::get_u32(r.data(), 44) == 9u && mavlink::wire::get_u16(r.data(), 96) == 0x0FFC && r[100] == 12 && r[101] == 1);
    }

    // OUTPUT_SET -> kOutputSetRequest
    mavlink::Message m;
    m.msgid = product::msg::OutputSet::kId;
    product::msg::OutputSet os;
    os.mask = 0xC00; os.value = 0x400;
    os.pack(m.payload);
    d.on_message(m, services, tx);
    bu::OutputSetRequest o{};
    CHECK(!rec.events.empty() && rec.events.back().get(o) && o.mask == 0xC00 && o.value == 0x400);

    // SERIAL_CONTROL (GPS1, không phải REPLY) -> kGnssTxRequest; REPLY bị bỏ qua
    mavlink::SerialControl sc;
    sc.device = mavlink::SERIAL_CONTROL_DEV_GPS1; sc.flags = mavlink::SERIAL_CONTROL_FLAG_RESPOND;
    sc.count = 2; sc.data[0] = 'O'; sc.data[1] = 'K'; sc.baudrate = 9600;
    m = mavlink::Message{};
    m.msgid = mavlink::SerialControl::kId;
    sc.pack(m.payload);
    d.on_message(m, services, tx);
    bu::GnssTxRequest req{};
    CHECK(rec.events.back().id == bu::kGnssTxRequest && rec.events.back().get(req) && req.len == 2 && req.baudrate == 9600);
    const std::size_t n = rec.events.size();
    sc.flags = mavlink::SERIAL_CONTROL_FLAG_REPLY;
    sc.pack(m.payload);
    d.on_message(m, services, tx);
    CHECK_EQ(rec.events.size(), n);

    // MAV_CMD_DO_SET_RELAY
    mavlink::CommandLong c;
    c.command = mavlink::MAV_CMD_DO_SET_RELAY; c.param[0] = 5; c.param[1] = 1;
    CHECK_EQ(d.on_command(c, services, tx), mavlink::MAV_RESULT_ACCEPTED);
    CHECK(rec.events.back().get(o) && o.mask == 0x20 && o.value == 0x20);
    c.param[0] = 40;
    CHECK_EQ(d.on_command(c, services, tx), mavlink::MAV_RESULT_DENIED);
    std::uint8_t extra = 0;
    CHECK(d.crc_extra(62003, extra) && extra == 2 && !d.crc_extra(62004, extra));
}
