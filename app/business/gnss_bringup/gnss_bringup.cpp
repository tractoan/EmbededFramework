#include "gnss_bringup/gnss_bringup.hpp"

#include <cstdlib>
#include <cstring>

#include "bringup_notify.hpp"
#include "osal/time.hpp"

namespace app {

using bringup::GnssState;
using bringup::Severity;

namespace {
constexpr const char* kTag = "gnss";
}

core::Status GnssBringup::on_init(SystemContext& ctx) {
    ctx_ = &ctx;
    if (cfg_.gnss_port >= 0 && (cfg_.receiver == nullptr || cfg_.ports == nullptr)) return core::Status::InvalidArg;
    core::Status st = ctx.params.register_params(defs_, sizeof(defs_) / sizeof(defs_[0]));
    if (core::is_ok(st)) st = ctx.cli.register_command({"gnss", "[send <text> | baud <bps>] - GNSS receiver", this});
    return st;
}

void GnssBringup::apply_params() {
    detect_ms_ = static_cast<std::uint32_t>(ctx_->params.get_or(params::kGnssDetectMs, 5000));
    forward_   = ctx_->params.get_or(params::kGnssForward, 1) != 0;
    if (cfg_.receiver != nullptr) cfg_.receiver->set_raw_tap(forward_);
}

core::Status GnssBringup::on_start() {
    apply_params();
    start_ms_ = osal::now_ms();
    if (cfg_.gnss_port < 0) {
        state_ = GnssState::Absent;
        return core::Status::Ok;
    }
    state_ = GnssState::Detecting;
    const core::Status st = cfg_.receiver->start(static_cast<std::uint32_t>(ctx_->params.get_or(params::kGnssBaud, 115200)));
    if (!core::is_ok(st)) return st;
    bringup::notify(*ctx_, kTag, Severity::Info, "GNSS: waiting data on port %d (%lu ms)", cfg_.gnss_port,
                    static_cast<unsigned long>(detect_ms_));
    return core::Status::Ok;
}

void GnssBringup::on_stop() {
    if (cfg_.receiver != nullptr) cfg_.receiver->stop();
}

void GnssBringup::forward_raw() {
    // Mỗi event tối đa kGnssChunkMax byte trong buffer (pool tĩnh), tối đa 4 event/tick (~100 KB/s).
    for (int k = 0; k < 4; ++k) {
        std::uint8_t chunk[bringup::kGnssChunkMax];
        const std::size_t n = cfg_.receiver->read_raw(chunk, sizeof(chunk));
        if (n == 0) break;
        msg::Buffer buf = msg::Buffer::copy_of(chunk, n);
        if (!buf) {   // hết pool: bỏ gói này (xem "sys buffers")
            raw_dropped_.fetch_add(1);
            continue;
        }
        ctx_->bus.publish(msg::Event::make(bringup::kGnssRxData, bringup::GnssDataHeader{static_cast<std::uint16_t>(n)},
                                           std::move(buf)));
    }
}

void GnssBringup::on_tick(std::uint64_t now_ms) {
    const GnssState before = state_;
    if (cfg_.gnss_port >= 0) {
        if (state_ == GnssState::Detecting) {
            if (cfg_.receiver->has_data()) {
                state_ = GnssState::Connected;
                bringup::notify(*ctx_, kTag, Severity::Notice, "GNSS: receiver detected on port %d", cfg_.gnss_port);
            } else if (now_ms >= start_ms_ + detect_ms_) {
                state_ = GnssState::Absent;
                cfg_.receiver->stop();
                cfg_.ports->set_mode(static_cast<std::size_t>(cfg_.gnss_port), svc::SerialMode::Loopback);
                bringup::notify(*ctx_, kTag, Severity::Warning, "GNSS: no data in %lus, port %d -> loopback",
                                static_cast<unsigned long>(detect_ms_ / 1000), cfg_.gnss_port);
            }
        }
        if (state_ == GnssState::Connected) {
            device::GnssFix fix{};
            std::uint32_t seq = 0;
            if (cfg_.receiver->latest_fix(fix, seq) && seq != last_fix_seq_) {
                last_fix_seq_ = seq;
                ctx_->bus.publish(bringup::make_record(bringup::kGnssFix, fix));
            }
            if (forward_) forward_raw();
        }
    }
    if (state_ != before || now_ms >= next_status_) {   // đổi trạng thái: báo ngay, không chờ chu kỳ 1 s
        next_status_ = now_ms + 1000;
        publish_status(now_ms);
    }
}

void GnssBringup::publish_status(std::uint64_t now_ms) {
    bringup::BringupStatus bs{};
    bs.uptime_ms    = static_cast<std::uint32_t>(now_ms);
    bs.gnss_state   = state_;
    bs.gcs_port     = cfg_.gcs_port >= 0 ? static_cast<std::uint8_t>(cfg_.gcs_port) : 0xFF;
    bs.gnss_port    = cfg_.gnss_port >= 0 ? static_cast<std::uint8_t>(cfg_.gnss_port) : 0xFF;
    bs.serial_count = static_cast<std::uint8_t>(cfg_.ports ? cfg_.ports->count() : 0);
    if (cfg_.receiver != nullptr) {
        const svc::GnssStats s = cfg_.receiver->stats();
        bs.gnss_rx_bytes = s.rx_bytes;
        bs.gnss_tx_bytes = s.tx_bytes;
        bs.gnss_baudrate = s.baudrate;
    }
    ctx_->bus.publish(bringup::make_record(bringup::kBringupStatus, bs));
}

void GnssBringup::on_event(const msg::Event& e) {
    svc::ParamChanged pc{};
    if (e.id == svc::evt::kParamChanged && e.get(pc)) {
        if (pc.id == params::kGnssDetectMs || pc.id == params::kGnssForward) apply_params();
        if (pc.id == params::kGnssBaud && state_ == GnssState::Connected) {
            cfg_.receiver->send(msg::Buffer{}, static_cast<std::uint32_t>(pc.value));
        }
        return;
    }
    bringup::GnssTxHeader req{};
    if (e.id != bringup::kGnssTxRequest || !e.get(req)) return;
    if (cfg_.gnss_port < 0 || state_ != GnssState::Connected) {
        bringup::notify(*ctx_, kTag, Severity::Warning, "GNSS: not connected, command dropped");
        return;
    }
    if (!e.buffer && req.baudrate == 0) return;
    if (!core::is_ok(cfg_.receiver->send(e.buffer, req.baudrate))) {   // Buffer chỉ tăng tham chiếu
        bringup::notify(*ctx_, kTag, Severity::Warning, "GNSS: tx queue full, command dropped");
        return;
    }
    if (req.baudrate != 0) {
        bringup::notify(*ctx_, kTag, Severity::Notice, "GNSS: port baudrate -> %lu", static_cast<unsigned long>(req.baudrate));
    }
}

int GnssBringup::on_command(int argc, char** argv, svc::CommandOutput& out) {
    // Lệnh chạy trên thread của kênh -> chỉ publish yêu cầu, module xử lý trên thread ứng dụng.
    if (argc >= 3 && std::strcmp(argv[1], "send") == 0) {
        char line[core::cfg::kCliLineMax + 2];
        std::size_t n = 0;
        for (int a = 2; a < argc; ++a) {   // ghép lại các từ (CommandService đã tách theo khoảng trắng)
            for (const char* c = argv[a]; *c != '\0' && n < sizeof(line) - 2; ++c) line[n++] = *c;
            if (a + 1 < argc && n < sizeof(line) - 2) line[n++] = ' ';
        }
        line[n++] = '\r';
        line[n++] = '\n';
        msg::Buffer buf = msg::Buffer::copy_of(line, n);
        if (!buf) {
            out.print("no buffer available\r\n");
            return -1;
        }
        ctx_->bus.publish(msg::Event::make(bringup::kGnssTxRequest, bringup::GnssTxHeader{0}, std::move(buf)));
        out.print("queued %u bytes\r\n", static_cast<unsigned>(n));
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "baud") == 0) {
        const bringup::GnssTxHeader req{static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10))};
        ctx_->bus.publish(msg::Event::make(bringup::kGnssTxRequest, req));
        return 0;
    }
    if (cfg_.gnss_port < 0 || cfg_.receiver == nullptr) {
        out.print("no GNSS port\r\n");
        return 0;
    }
    const svc::GnssStats s = cfg_.receiver->stats();
    out.print("port %d state=%s rx=%lu tx=%lu baud=%lu forward=%d raw_dropped=%lu tx_dropped=%lu\r\n", cfg_.gnss_port,
              bringup::to_string(state_.load()), static_cast<unsigned long>(s.rx_bytes),
              static_cast<unsigned long>(s.tx_bytes), static_cast<unsigned long>(s.baudrate), forward_.load() ? 1 : 0,
              static_cast<unsigned long>(s.raw_dropped + raw_dropped_.load()), static_cast<unsigned long>(s.tx_dropped));
    device::GnssFix f{};
    std::uint32_t seq = 0;
    const bool has = cfg_.receiver->latest_fix(f, seq);
    const std::uint32_t t = f.time_ms;
    out.print("NMEA: %lu sentences, %lu fix, %lu checksum errors | UTC %02lu:%02lu:%02lu.%03lu%s | ",
              static_cast<unsigned long>(s.sentences), static_cast<unsigned long>(seq),
              static_cast<unsigned long>(s.checksum_errors), static_cast<unsigned long>(t / 3600000),
              static_cast<unsigned long>(t / 60000 % 60), static_cast<unsigned long>(t / 1000 % 60),
              static_cast<unsigned long>(t % 1000), (has && (f.flags & device::GnssFix::kTimeValid)) ? "" : " (invalid)");
    if (has && (f.flags & device::GnssFix::kPositionValid)) {
        const std::int32_t lat = f.lat_e7, lon = f.lon_e7;
        out.print("lat %s%ld.%07ld lon %s%ld.%07ld q=%u sats=%u\r\n", lat < 0 ? "-" : "",
                  static_cast<long>((lat < 0 ? -lat : lat) / 10000000), static_cast<long>((lat < 0 ? -lat : lat) % 10000000),
                  lon < 0 ? "-" : "", static_cast<long>((lon < 0 ? -lon : lon) / 10000000),
                  static_cast<long>((lon < 0 ? -lon : lon) % 10000000), static_cast<unsigned>(f.quality),
                  static_cast<unsigned>(f.satellites));
    } else {
        out.print("no position fix\r\n");
    }
    return 0;
}

}  // namespace app
