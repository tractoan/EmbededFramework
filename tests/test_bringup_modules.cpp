// Module bring-up: phát hiện máy thu GNSS trong 5 s, chuyển cổng sang loopback, chuyển tiếp dữ liệu/lệnh,
// đặt output, báo IMU; dialect MAVLink ánh xạ Event <-> bản tin.
#include <cmath>

#include "bringup_events.hpp"
#include "mavlink2/bringup/messages.hpp"
#include "imu_bringup/imu_bringup.hpp"
#include "io_bringup/io_bringup.hpp"
#include "mavlink_dialect.hpp"
#include "mocks.hpp"
#include "osal/time.hpp"
#include "gnss_bringup/gnss_bringup.hpp"
#include "serial_bringup/serial_bringup.hpp"
#include "services/gnss_receiver.hpp"
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

// Máy thu GNSS giả (không thread): test chính sách của GnssBringup.
struct FakeGnss final : svc::IGnssReceiver {
    bool started = false, data = false, tap = false;
    std::uint32_t start_baud = 0, seq = 0;
    device::GnssFix fix{};
    std::string raw;
    std::vector<std::pair<std::string, std::uint32_t>> sent;   // (dữ liệu, baud)
    bool full = false;
    core::Status start(std::uint32_t baud) override {
        started    = true;
        start_baud = baud;
        return core::Status::Ok;
    }
    void stop() override { started = false; }
    bool has_data() const override { return data; }
    bool latest_fix(device::GnssFix& out, std::uint32_t& s) const override {
        out = fix;
        s   = seq;
        return seq != 0;
    }
    void set_raw_tap(bool e) override { tap = e; }
    std::size_t read_raw(std::uint8_t* d, std::size_t max) override {
        const std::size_t k = std::min(max, raw.size());
        std::memcpy(d, raw.data(), k);
        raw.erase(0, k);
        return k;
    }
    core::Status send(msg::Buffer b, std::uint32_t baud) override {
        if (full) return core::Status::Busy;
        sent.emplace_back(std::string(reinterpret_cast<const char*>(b.data()), b.size()), baud);
        return core::Status::Ok;
    }
    svc::GnssStats stats() const override {
        svc::GnssStats st;
        st.rx_bytes = data ? 100 : 0;
        st.baudrate = start_baud;
        st.fixes    = seq;
        return st;
    }
};

TEST(serial_bringup_loopback_report_and_roles) {
    Ctx c;
    FakePorts ports;
    ports.st[0].mode = svc::SerialMode::External;   // giao diện
    ports.st[1].mode = svc::SerialMode::Stream;     // máy thu GNSS
    app::SerialBringup m({&ports});
    CHECK(core::is_ok(m.on_init(c.ctx)));
    CHECK(core::is_ok(m.on_start()));
    CHECK(ports.started && ports.period == 200u && m.tick_period_ms() == 500u);

    ports.st[2].mode = svc::SerialMode::Loopback; ports.st[2].last = svc::LoopResult::Pass; ports.st[2].pass = 7;
    ports.st[3].mode = svc::SerialMode::Loopback; ports.st[3].last = svc::LoopResult::Timeout; ports.st[3].fail = 2;
    m.on_tick(osal::now_ms());
    bu::LoopbackHeader ls{};
    CHECK(c.last(bu::kLoopbackStatus, ls));
    CHECK_EQ(ls.count, 12);
    const msg::Event* le = c.last_event(bu::kLoopbackStatus);
    bu::PortResult pr[12]{};
    CHECK(le != nullptr && le->buffer.size() == sizeof(pr));
    if (le != nullptr && le->buffer.size() == sizeof(pr)) std::memcpy(pr, le->buffer.data(), sizeof(pr));
    CHECK(pr[0].role == static_cast<std::uint8_t>(bu::SerialRole::Gcs));
    CHECK(pr[1].role == static_cast<std::uint8_t>(bu::SerialRole::Gnss));
    CHECK(pr[2].role == static_cast<std::uint8_t>(bu::SerialRole::Loopback));
    CHECK(pr[4].role == static_cast<std::uint8_t>(bu::SerialRole::Disabled));
    CHECK((ls.ok_mask & 0x4) && !(ls.ok_mask & 0x8) && (ls.tested_mask & 0xC) == 0xC && !(ls.tested_mask & 0x3));
    CHECK_EQ(pr[2].pass, 7u);
    CHECK(c.has_text("Serial 2 loopback PASS") && c.has_text("Serial 3 loopback FAIL (TIMEOUT)"));

    // GNSS vắng -> cổng thành Loopback -> được báo như cổng loopback thường
    ports.st[1].mode = svc::SerialMode::Loopback; ports.st[1].last = svc::LoopResult::Pass;
    m.on_tick(osal::now_ms());
    CHECK(c.last(bu::kLoopbackStatus, ls) && (ls.ok_mask & 0x2) && c.has_text("Serial 1 loopback PASS"));
}

TEST(gnss_bringup_absent_switches_port_to_loopback) {
    Ctx c;
    FakePorts ports;
    FakeGnss rx;
    app::GnssBringup m({&rx, &ports, 1, 0});
    CHECK(core::is_ok(m.on_init(c.ctx)));
    c.params.set(app::params::kGnssBaud, 9600);   // tham số đã nạp từ NV trước on_start
    CHECK(core::is_ok(m.on_start()));
    const std::uint64_t t0 = osal::now_ms();
    CHECK(rx.started && rx.start_baud == 9600 && rx.tap);

    m.on_tick(t0 + 4900);
    CHECK(m.state() == bu::GnssState::Detecting);
    m.on_tick(t0 + 5100);   // hết cửa sổ 5 s, receiver chưa nhận byte nào
    CHECK(m.state() == bu::GnssState::Absent && !rx.started);
    CHECK(ports.st[1].mode == svc::SerialMode::Loopback);
    CHECK(c.has_text("port 1 -> loopback"));

    // Lệnh GNSS khi không có máy thu bị bỏ, có thông báo
    m.on_event(msg::Event::make(bu::kGnssTxRequest, bu::GnssTxHeader{0}, msg::Buffer::copy_of("ABC", 3)));
    CHECK(rx.sent.empty() && c.has_text("command dropped"));
    bu::BringupStatus bs{};
    CHECK(c.last_record(bu::kBringupStatus, bs) && bs.gnss_state == bu::GnssState::Absent && bs.gnss_port == 1 &&
          bs.gcs_port == 0 && bs.serial_count == 12);
}

TEST(gnss_bringup_connected_publishes_fix_raw_and_commands) {
    Ctx c;
    FakePorts ports;
    FakeGnss rx;
    app::GnssBringup m({&rx, &ports, 1, 0});
    m.on_init(c.ctx);
    m.on_start();
    const std::uint64_t t0 = osal::now_ms();
    rx.data = true;
    rx.raw  = std::string(100, 'N');
    m.on_tick(t0 + 1200);
    CHECK(m.state() == bu::GnssState::Connected && ports.st[1].mode != svc::SerialMode::Loopback);
    CHECK_EQ(c.rec.count(bu::kGnssRxData), 1u);   // 100 byte trong một buffer
    const msg::Event* ge = c.last_event(bu::kGnssRxData);
    CHECK(ge != nullptr && Ctx::text_of(*ge) == std::string(100, 'N'));
    m.on_tick(t0 + 9000);   // đã kết nối: không bao giờ chuyển sang loopback
    CHECK(m.state() == bu::GnssState::Connected);

    // Fix mới (seq tăng) -> đúng một kGnssFix; seq không đổi -> không phát lại
    rx.fix.lat_e7 = 481173000; rx.fix.lon_e7 = 115166667; rx.fix.time_ms = 45319000;
    rx.fix.flags  = device::GnssFix::kTimeValid | device::GnssFix::kPositionValid;
    rx.seq        = 1;
    m.on_tick(t0 + 9020);
    m.on_tick(t0 + 9040);
    device::GnssFix fix{};
    CHECK(c.last_record(bu::kGnssFix, fix) && fix.lat_e7 == 481173000 && c.rec.count(bu::kGnssFix) == 1u);

    // Lệnh tới máy thu -> hàng đợi gửi của service (không chặn thread ứng dụng)
    m.on_event(msg::Event::make(bu::kGnssTxRequest, bu::GnssTxHeader{38400}, msg::Buffer::copy_of("CFG\r\n", 5)));
    CHECK(rx.sent.size() == 1 && rx.sent[0].first == "CFG\r\n" && rx.sent[0].second == 38400);
    rx.full = true;
    m.on_event(msg::Event::make(bu::kGnssTxRequest, bu::GnssTxHeader{0}, msg::Buffer::copy_of("X", 1)));
    CHECK(c.has_text("tx queue full"));
    rx.full = false;

    // CLI "gnss send ..." -> event yêu cầu; "gnss" -> trạng thái + fix
    char line[] = "gnss send $PCMD,1 2";
    CaptureOut out;
    c.cli.execute(line, out);
    const msg::Event* q = c.last_event(bu::kGnssTxRequest);
    CHECK(q != nullptr && Ctx::text_of(*q) == "$PCMD,1 2\r\n");
    char gl[] = "gnss";
    CaptureOut fo;
    c.cli.execute(gl, fo);
    CHECK(fo.text.find("state=CONNECTED") != std::string::npos && fo.text.find("UTC 12:35:19.000") != std::string::npos &&
          fo.text.find("lat 48.1173000 lon 11.5166667") != std::string::npos);

    // gnss.forward = 0 -> tắt tap của service, không chuyển tiếp
    c.params.set(app::params::kGnssForward, 0);
    for (const auto& e : std::vector<msg::Event>(c.rec.events)) m.on_event(e);
    CHECK(!rx.tap);
    const std::size_t before = c.rec.count(bu::kGnssRxData);
    rx.raw = "XYZ";
    m.on_tick(t0 + 9100);
    CHECK_EQ(c.rec.count(bu::kGnssRxData), before);
}

TEST(gnss_receiver_service_parses_taps_and_sends) {
    PoolScope pool;
    FakePorts ports;
    svc::GnssReceiver rx;
    svc::GnssReceiver::Config cfg;
    cfg.ports = &ports;
    cfg.port  = 1;
    CHECK(rx.init(cfg) == core::Status::Ok);
    cfg.port = 20;
    svc::GnssReceiver bad;
    CHECK(bad.init(cfg) == core::Status::InvalidArg);

    // Không chạy thread: điều khiển từng vòng bằng process() (thread thật chạy đúng vòng này)
    CHECK(!rx.has_data());
    rx.set_raw_tap(true);
    std::uint64_t now = 1000;
    ports.incoming(1, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");
    rx.process(0, now);
    device::GnssFix f{};
    std::uint32_t seq = 0;
    CHECK(rx.has_data() && rx.latest_fix(f, seq) && seq == 1);   // chưa thấy GGA: gom fix theo RMC
    ports.incoming(1, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    rx.process(0, now += 100);
    CHECK(rx.latest_fix(f, seq) && seq == 2 && f.year == 1994 && f.alt_mm == 545400);
    ports.incoming(1, "$GPRMC,123520,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*60\r\n");
    rx.process(0, now += 900);   // đã có GGA đều đặn: RMC không tạo fix mới
    CHECK(rx.latest_fix(f, seq) && seq == 2);
    ports.incoming(1, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00\r\n");   // sai checksum
    rx.process(0, now += 3000);
    svc::GnssStats st = rx.stats();
    CHECK(st.sentences == 3 && st.checksum_errors == 1 && st.fixes == 2 && st.last_rx_ms == now && st.rx_bytes > 200);

    // Tap: đúng byte thô theo thứ tự
    std::uint8_t raw[512];
    const std::size_t n = rx.read_raw(raw, sizeof(raw));
    CHECK(n == st.rx_bytes && std::memcmp(raw, "$GPRMC,123519", 13) == 0);

    // Gửi lệnh: hàng đợi (không chặn) -> ghi + đổi baud ở vòng xử lý
    CHECK(rx.send(msg::Buffer::copy_of("CFG\r\n", 5), 38400) == core::Status::Ok);
    CHECK(rx.send(msg::Buffer{}, 0) == core::Status::InvalidArg);
    for (std::size_t k = 1; k < svc::GnssReceiver::kTxQueue; ++k) rx.send(msg::Buffer::copy_of("Q", 1));
    CHECK(rx.send(msg::Buffer::copy_of("Z", 1)) == core::Status::Busy);
    rx.process(0, now);
    CHECK(ports.tx[1] == "CFG\r\n" && ports.st[1].baudrate == 38400 && rx.stats().baudrate == 38400);
    st = rx.stats();
    CHECK(st.tx_bytes == 5 && st.tx_dropped == 1);
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
        core::Status port = core::Status::Ok;
        device::ImuStats st{};
        const char* format_name() const override { return "ln200"; }
        std::uint8_t type_id() const override { return 1; }
        core::Status status() const override { return port; }
        core::Status read(device::ImuSample&, std::uint32_t) override { return core::Status::Timeout; }
        device::ImuStats stats() const override { return st; }
    } a, b;
    b.port = core::Status::NotFound;
    msg::Topic<device::ImuSample> ta, tb;   // mẫu do nhóm lấy mẫu publish (ở đây: test publish)
    const app::ImuBringup::Channel list[] = {{&a, &ta}, {&b, &tb}};
    Ctx c;
    app::ImuBringup m(list, 2);
    CHECK(core::is_ok(m.on_init(c.ctx)));
    CHECK(core::is_ok(m.on_start()));   // IMU lỗi không làm hỏng khởi động
    CHECK(c.has_text("IMU1 (ln200): port failed"));
    CHECK_EQ(m.tick_period_ms(), 100u);

    const std::uint64_t now = osal::now_ms() + 1000;
    a.st.frames = 400; a.st.rate_hz = 400; a.st.last_frame_ms = now - 10;
    device::ImuSample sa{};
    sa.gyro[2] = 0.17f; sa.raw_len = 2; sa.raw[0] = 0xAB;
    ta.publish(sa, now - 10);
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
    device::GnssFix gf{};
    gf.time_ms = 45319000; gf.year = 1994; gf.month = 3; gf.day = 23; gf.lat_e7 = 481173000; gf.lon_e7 = -115166667;
    gf.alt_mm = 545400; gf.quality = 1; gf.satellites = 8; gf.hdop_x100 = 90;
    gf.flags = device::GnssFix::kTimeValid | device::GnssFix::kDateValid | device::GnssFix::kPositionValid |
               device::GnssFix::kAltitudeValid;
    d.on_event(bu::make_record(bu::kGnssFix, gf), tx);
    CHECK_EQ(tx.sent.size(), 7u);
    if (tx.sent.size() == 7) {
        const auto& g = tx.sent[5].payload;   // GNSS_FIX
        CHECK_EQ(tx.sent[5].id, 62005u);
        const std::uint64_t t = mavlink2::wire::get_u32(g.data(), 0) | (std::uint64_t{mavlink2::wire::get_u32(g.data(), 4)} << 32);
        CHECK_EQ(t, 764426119000000ull);
        CHECK(mavlink2::wire::get_u32(g.data(), 8) == 45319000u && static_cast<std::int32_t>(mavlink2::wire::get_u32(g.data(), 16)) == -115166667);
        CHECK(mavlink2::wire::get_u16(g.data(), 24) == 1994 && g[32] == 3 && g[33] == 23 && g[35] == 8 && g[36] == 15);
        const auto& r = tx.sent[6].payload;   // GPS_RAW_INT
        CHECK(tx.sent[6].id == mavlink2::GpsRawInt::kId && r[28] == mavlink2::GPS_FIX_TYPE_3D_FIX && r[29] == 8);
        CHECK(static_cast<std::int32_t>(mavlink2::wire::get_u32(r.data(), 8)) == 481173000 && mavlink2::wire::get_u16(r.data(), 20) == 90);
    }

    if (tx.sent.size() >= 5) {
        const auto& p = tx.sent[0].payload;
        CHECK_EQ(tx.sent[0].id, mavlink2::bringup::ImuData::kId);
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
    m.msgid = mavlink2::bringup::OutputSet::kId;
    mavlink2::bringup::OutputSet os;
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
