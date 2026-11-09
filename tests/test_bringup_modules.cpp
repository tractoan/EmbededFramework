// Module bring-up: phát hiện máy thu GNSS trong 5 s, chuyển cổng sang loopback, chuyển tiếp dữ liệu/lệnh,
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
#include "messaging/buffer_allocators.hpp"
#include "test_framework.hpp"

namespace {
namespace bu = app::bringup;

// Pool tĩnh cho Event::buffer trong test (giống pool sản phẩm cài lúc khởi động).
struct PoolScope {
    PoolScope() : prev_(msg::Buffer::default_allocator()) { msg::Buffer::set_default_allocator(&pools()); }
    ~PoolScope() { msg::Buffer::set_default_allocator(prev_); }
    static msg::PoolAllocator& pools() {
        static msg::StaticBlockPool<256, 96> small;
        static msg::StaticBlockPool<1024, 8> large;
        static msg::PoolAllocator p({&small, &large});
        return p;
    }

private:
    msg::IBufferAllocator* prev_;
};

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
    PoolScope pool;
    msg::EventBus bus;
    svc::ParamStore params;
    svc::CommandService cli;
    svc::LedService leds;
    app::Devices devices;
    app::SystemContext ctx{bus, params, cli, leds, devices};
    mock::Recorder rec;
    Ctx() {
        params.init(nullptr, &bus);
        bus.subscribe(msg::evt::kAny, &rec);
    }
    const msg::Event* last_event(msg::EventId id) const {
        for (auto it = rec.events.rbegin(); it != rec.events.rend(); ++it) {
            if (it->id == id) return &*it;
        }
        return nullptr;
    }
    template <typename T>
    bool last(msg::EventId id, T& out) const {   // payload inline
        const msg::Event* e = last_event(id);
        return e != nullptr && e->get(out);
    }
    template <typename T>
    bool last_record(msg::EventId id, T& out) const {   // struct trong Event::buffer
        const msg::Event* e = last_event(id);
        return e != nullptr && bu::get_record(*e, out);
    }
    static std::string text_of(const msg::Event& e) {
        return std::string(reinterpret_cast<const char*>(e.buffer.data()), e.buffer.size());
    }
    bool has_text(const char* needle) const {
        for (const auto& e : rec.events) {
            if (e.id == bu::kStatusText && text_of(e).find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

struct CaptureOut final : svc::CommandOutput {
    std::string text;
    void write(const char* t, std::size_t n) override { text.append(t, n); }
};

struct TxCapture final : mavlink2::Sender {
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
    m.on_event(msg::Event::make(bu::kGnssTxRequest, bu::GnssTxHeader{0}, msg::Buffer::copy_of("ABC", 3)));
    CHECK(ports.tx[1].empty() && c.has_text("command dropped"));

    // Báo cáo loopback: vai trò + kết quả từng cổng
    ports.st[2].mode = svc::SerialMode::Loopback; ports.st[2].last = svc::LoopResult::Pass; ports.st[2].pass = 7;
    ports.st[3].mode = svc::SerialMode::Loopback; ports.st[3].last = svc::LoopResult::Timeout; ports.st[3].fail = 2;
    ports.st[1].last = svc::LoopResult::Pass;
    m.on_tick(t0 + 10000);
    bu::LoopbackHeader ls{};
    CHECK(c.last(bu::kLoopbackStatus, ls));
    CHECK_EQ(ls.count, 12);
    const msg::Event* le = c.last_event(bu::kLoopbackStatus);
    CHECK(le != nullptr && le->buffer.size() == 12 * sizeof(bu::PortResult));
    bu::PortResult pr[12]{};
    if (le != nullptr && le->buffer.size() == sizeof(pr)) std::memcpy(pr, le->buffer.data(), sizeof(pr));
    CHECK(pr[0].role == static_cast<std::uint8_t>(bu::SerialRole::Gcs));
    CHECK(pr[1].role == static_cast<std::uint8_t>(bu::SerialRole::Loopback));
    CHECK((ls.ok_mask & 0x6) == 0x6 && (ls.ok_mask & 0x8) == 0);
    CHECK((ls.tested_mask & 0xE) == 0xE && (ls.tested_mask & 0x1) == 0);
    CHECK_EQ(pr[2].pass, 7u);
    CHECK(c.has_text("Serial 3 loopback FAIL (TIMEOUT)"));
    bu::BringupStatus bs{};
    CHECK(c.last_record(bu::kBringupStatus, bs) && bs.gnss_state == bu::GnssState::Absent && bs.gnss_port == 1);
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
    CHECK_EQ(c.rec.count(bu::kGnssRxData), 1u);   // 100 byte trong một buffer
    const msg::Event* ge = c.last_event(bu::kGnssRxData);
    CHECK(ge != nullptr && ge->buffer.size() == 100 && Ctx::text_of(*ge) == std::string(100, 'N'));
    m.on_tick(t0 + 9000);                          // đã kết nối: không bao giờ chuyển sang loopback
    CHECK(m.gnss_state() == bu::GnssState::Connected);

    m.on_event(msg::Event::make(bu::kGnssTxRequest, bu::GnssTxHeader{38400}, msg::Buffer::copy_of("CFG\r\n", 5)));
    CHECK(ports.tx[1] == "CFG\r\n" && ports.st[1].baudrate == 38400);

    // CLI "gnss send ..." -> event yêu cầu (thread kênh không ghi trực tiếp)
    char line[] = "gnss send $PCMD,1 2";
    CaptureOut out;
    c.cli.execute(line, out);
    const msg::Event* q = c.last_event(bu::kGnssTxRequest);
    CHECK(q != nullptr && Ctx::text_of(*q) == "$PCMD,1 2\r\n");

    // gnss.forward = 0 -> không chuyển tiếp
    c.params.set(app::params::kGnssForward, 0);
    for (const auto& e : std::vector<msg::Event>(c.rec.events)) m.on_event(e);
    const std::size_t before = c.rec.count(bu::kGnssRxData);
    ports.incoming(1, "XYZ");
    m.on_tick(t0 + 9100);
    CHECK_EQ(c.rec.count(bu::kGnssRxData), before);
}

TEST(io_bringup_outputs_and_inputs) {
    Ctx c;
    mock::Gpio o[20], i[20];
    device::GpioOutputBank outs;
    device::GpioInputBank ins;
    for (auto& p : o) outs.add(&p);
    for (auto& p : i) ins.add(&p);
    app::IoBringup m(&ins, &outs);
    CHECK(core::is_ok(m.on_init(c.ctx)));
    m.on_start();
    m.on_tick(1000);
    bu::IoStatus s{};
    CHECK(c.last(bu::kIoStatus, s) && s.input_count == 20 && s.output_count == 20 && s.inputs == 0);

    m.on_event(msg::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{0x00C00, 0x00400}));
    CHECK(o[10].read() && !o[11].read());
    CHECK(c.last(bu::kIoStatus, s) && s.outputs == 0x400u);

    const std::size_t n = c.rec.count(bu::kIoStatus);
    m.on_tick(1020);                     // không đổi, chưa tới chu kỳ báo cáo
    CHECK_EQ(c.rec.count(bu::kIoStatus), n);
    i[19].write(true);                   // input đổi -> báo ngay
    m.on_tick(1040);
    CHECK(c.last(bu::kIoStatus, s) && s.inputs == (1u << 19));

    m.on_event(msg::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{1u << 25, 1u << 25}));   // kênh không có
    CHECK(c.has_text("output set"));
}

TEST(imu_bringup_reports_samples_and_failures) {
    struct FakeImu final : device::IImu {
        bool fail_start = false, has = false;
        device::ImuStats st{};
        device::ImuSample s{};
        const char* format_name() const override { return "ln200"; }
        std::uint8_t type_id() const override { return 1; }
        core::Status start() override { return fail_start ? core::Status::NotFound : core::Status::Ok; }
        void stop() override {}
        bool latest(device::ImuSample& out) const override { out = s; return has; }
        device::ImuStats stats() const override { return st; }
    } a, b;
    b.fail_start = true;
    device::IImu* list[] = {&a, &b};
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
        if (e.id != bu::kImuSample || !bu::get_record(e, ev)) continue;
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
    PoolScope pool;
    product::BringupDialect d;
    TxCapture tx;
    msg::EventBus bus;
    svc::ParamStore params;
    params.init(nullptr, &bus);
    svc::CommandService commands;
    comm::Services services{bus, params, commands};
    mock::Recorder rec;
    bus.subscribe(msg::evt::kAny, &rec);

    bu::ImuSample s{};
    s.imu = 1; s.type = 2; s.frames = 77; s.gyro[0] = 1.5f; s.accel[2] = -9.8f; s.rate_hz = 100; s.raw_len = 3; s.raw[2] = 0x5A;
    d.on_event(bu::make_record(bu::kImuSample, s), tx);
    const std::string gnss = "$GPG" + std::string(96, 'x');   // 100 byte -> 2 SERIAL_CONTROL (70 + 30)
    d.on_event(msg::Event::make(bu::kGnssRxData, bu::GnssDataHeader{100}, msg::Buffer::copy_of(gnss.data(), gnss.size())), tx);
    bu::PortResult pr[16]{};   // 16 cổng -> bản tin chỉ chở 12 cổng đầu
    pr[11].pass = 9; pr[0].role = 1;
    d.on_event(msg::Event::make(bu::kLoopbackStatus, bu::LoopbackHeader{0xFFFC, 0xFFFF, 16}, msg::Buffer::copy_of(pr, sizeof(pr))), tx);
    d.on_event(msg::Event::make(bu::kStatusText, bu::StatusHeader{bu::Severity::Error}, msg::Buffer::copy_of("boom", 4)), tx);
    CHECK_EQ(tx.sent.size(), 5u);
    if (tx.sent.size() == 5) {
        const auto& p = tx.sent[0].payload;
        CHECK_EQ(tx.sent[0].id, product::mavmsg::ImuData::kId);
        CHECK_EQ(mavlink2::wire::get_u32(p.data(), 4), 77u);
        CHECK(mavlink2::wire::get_f32(p.data(), 12) == 1.5f && mavlink2::wire::get_f32(p.data(), 32) == -9.8f);
        CHECK(p[44] == 1 && p[45] == 2 && p[47] == 3 && p[50] == 0x5A);
        const auto& q = tx.sent[1].payload;   // SERIAL_CONTROL REPLY, device GPS1
        CHECK(tx.sent[1].id == mavlink2::SerialControl::kId && q[6] == 1 && q[7] == 1 && q[8] == 70 && q[9] == '$');
        CHECK(tx.sent[2].id == mavlink2::SerialControl::kId && tx.sent[2].payload[8] == 30);
        const auto& r = tx.sent[3].payload;
        CHECK(mavlink2::wire::get_u32(r.data(), 44) == 9u && mavlink2::wire::get_u16(r.data(), 96) == 0x0FFC && r[100] == 12 && r[101] == 1);
        CHECK(tx.sent[4].id == mavlink2::StatusText::kId && tx.sent[4].payload[0] == 3 && std::memcmp(&tx.sent[4].payload[1], "boom", 5) == 0);
    }

    // OUTPUT_SET -> kOutputSetRequest
    mavlink2::Message m;
    m.msgid = product::mavmsg::OutputSet::kId;
    product::mavmsg::OutputSet os;
    os.mask = 0xC00; os.value = 0x400;
    os.pack(m.payload);
    d.on_message(m, services, tx);
    bu::OutputSetRequest o{};
    CHECK(!rec.events.empty() && rec.events.back().get(o) && o.mask == 0xC00 && o.value == 0x400);

    // SERIAL_CONTROL (GPS1, không phải REPLY) -> kGnssTxRequest; REPLY bị bỏ qua
    mavlink2::SerialControl sc;
    sc.device = mavlink2::SERIAL_CONTROL_DEV_GPS1; sc.flags = mavlink2::SERIAL_CONTROL_FLAG_RESPOND;
    sc.count = 2; sc.data[0] = 'O'; sc.data[1] = 'K'; sc.baudrate = 9600;
    m = mavlink2::Message{};
    m.msgid = mavlink2::SerialControl::kId;
    sc.pack(m.payload);
    d.on_message(m, services, tx);
    bu::GnssTxHeader req{};
    CHECK(rec.events.back().id == bu::kGnssTxRequest && rec.events.back().get(req) && req.baudrate == 9600 &&
          rec.events.back().buffer.size() == 2 && rec.events.back().buffer.data()[0] == 'O');
    const std::size_t n = rec.events.size();
    sc.flags = mavlink2::SERIAL_CONTROL_FLAG_REPLY;
    sc.pack(m.payload);
    d.on_message(m, services, tx);
    CHECK_EQ(rec.events.size(), n);

    // MAV_CMD_DO_SET_RELAY
    mavlink2::CommandLong c;
    c.command = mavlink2::MAV_CMD_DO_SET_RELAY; c.param[0] = 5; c.param[1] = 1;
    CHECK_EQ(d.on_command(c, services, tx), mavlink2::MAV_RESULT_ACCEPTED);
    CHECK(rec.events.back().get(o) && o.mask == 0x20 && o.value == 0x20);
    c.param[0] = 40;
    CHECK_EQ(d.on_command(c, services, tx), mavlink2::MAV_RESULT_DENIED);
    std::uint8_t extra = 0;
    CHECK(d.crc_extra(62003, extra) && extra == 2 && !d.crc_extra(62004, extra));
}
