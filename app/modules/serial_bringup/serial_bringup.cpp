#include "serial_bringup/serial_bringup.hpp"

#include "bringup_notify.hpp"

#include <cstdlib>
#include <cstring>

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace app {

using bringup::GnssState;
using bringup::Severity;
using bringup::SerialRole;

namespace {
constexpr const char* kTag = "serial";
}

core::Status SerialBringup::on_init(SystemContext& ctx) {
    ctx_ = &ctx;
    if (cfg_.ports == nullptr || cfg_.ports->count() > bringup::kMaxSerial) return core::Status::InvalidArg;
    core::Status st = ctx.params.register_params(defs_, sizeof(defs_) / sizeof(defs_[0]));
    if (core::is_ok(st)) st = ctx.cli.register_command({"serial", "[reset] - serial loopback status", this});
    if (core::is_ok(st)) st = ctx.cli.register_command({"gnss", "[send <text> | baud <bps>] - GNSS receiver", this});
    return st;
}

void SerialBringup::apply_params() {
    const auto get = [&](svc::ParamId id, std::int32_t def) { return ctx_->params.get_or(id, def); };
    cfg_.ports->set_test_timing(static_cast<std::uint32_t>(get(params::kSerPeriodMs, 200)),
                                static_cast<std::uint32_t>(get(params::kSerTimeoutMs, 50)));
    report_ms_ = static_cast<std::uint32_t>(get(params::kSerReportMs, 500));
    detect_ms_ = static_cast<std::uint32_t>(get(params::kGnssDetectMs, 5000));
    forward_   = get(params::kGnssForward, 1) != 0;
}

core::Status SerialBringup::on_start() {
    apply_params();
    start_ms_ = osal::now_ms();
    if (cfg_.gnss_port >= 0) {
        const auto g = static_cast<std::size_t>(cfg_.gnss_port);
        cfg_.ports->set_baudrate(g, static_cast<std::uint32_t>(ctx_->params.get_or(params::kGnssBaud, 115200)));
        cfg_.ports->set_mode(g, svc::SerialMode::Stream);
        gnss_state_ = GnssState::Detecting;
    } else {
        gnss_state_ = GnssState::Absent;
    }
    const core::Status st = cfg_.ports->start();
    if (!core::is_ok(st)) return st;
    if (cfg_.gnss_port >= 0) {
        bringup::notify(*ctx_, kTag, Severity::Info, "GNSS: waiting data on port %d (%lu ms)", cfg_.gnss_port,
               static_cast<unsigned long>(detect_ms_));
    }
    return core::Status::Ok;
}

void SerialBringup::on_stop() { cfg_.ports->stop(); }

SerialRole SerialBringup::role(std::size_t port) const {
    if (static_cast<int>(port) == cfg_.gcs_port) return SerialRole::Gcs;
    if (static_cast<int>(port) == cfg_.gnss_port && gnss_state_ != GnssState::Absent) return SerialRole::Gnss;
    const svc::SerialMode m = cfg_.ports->stats(port).mode;
    return m == svc::SerialMode::Loopback ? SerialRole::Loopback : SerialRole::Disabled;
}

void SerialBringup::update_gnss(std::uint64_t now_ms) {
    if (cfg_.gnss_port < 0) return;
    const auto g = static_cast<std::size_t>(cfg_.gnss_port);

    if (gnss_state_ == GnssState::Detecting) {
        const svc::SerialPortStats s = cfg_.ports->stats(g);
        if (s.rx_bytes > 0) {
            gnss_state_ = GnssState::Connected;
            bringup::notify(*ctx_, kTag, Severity::Notice, "GNSS: receiver detected on port %d", cfg_.gnss_port);
        } else if (now_ms >= start_ms_ + detect_ms_) {
            gnss_state_ = GnssState::Absent;
            cfg_.ports->set_mode(g, svc::SerialMode::Loopback);
            bringup::notify(*ctx_, kTag, Severity::Warning, "GNSS: no data in %lus, port %d -> loopback",
                   static_cast<unsigned long>(detect_ms_ / 1000), cfg_.gnss_port);
            return;
        }
    }
    if (gnss_state_ != GnssState::Connected) return;

    // Chuyển tiếp dữ liệu máy thu (tối đa 16 gói/tick ~ 56 KB/s, đủ cho 460800 bps).
    for (int k = 0; k < 16; ++k) {
        bringup::GnssData d{};
        d.len = static_cast<std::uint8_t>(cfg_.ports->read(g, d.data, sizeof(d.data)));
        if (d.len == 0) break;
        if (forward_) ctx_->bus.publish(svc::Event::make(bringup::kGnssRxData, d));
    }
}

void SerialBringup::publish_loopback() {
    bringup::LoopbackStatus ls{};
    ls.count = static_cast<std::uint8_t>(cfg_.ports->count());
    for (std::size_t i = 0; i < ls.count; ++i) {
        const svc::SerialPortStats s = cfg_.ports->stats(i);
        ls.role[i]   = static_cast<std::uint8_t>(role(i));
        ls.result[i] = static_cast<std::uint8_t>(s.last);
        ls.pass[i]   = s.pass;
        ls.fail[i]   = s.fail;
        if (s.mode == svc::SerialMode::Loopback) ls.tested_mask = static_cast<std::uint16_t>(ls.tested_mask | (1u << i));
        if (s.mode == svc::SerialMode::Loopback && s.last == svc::LoopResult::Pass) {
            ls.ok_mask = static_cast<std::uint16_t>(ls.ok_mask | (1u << i));
        }
    }
    // Báo người vận hành khi một cổng đổi PASS <-> FAIL. Cổng chập chờn: tối đa 1 thông báo / kNoticeHoldMs
    // (trạng thái chi tiết luôn có trong LoopbackStatus).
    const std::uint64_t now = osal::now_ms();
    for (std::size_t i = 0; i < ls.count; ++i) {
        const auto bit = static_cast<std::uint16_t>(1u << i);
        if (!(ls.tested_mask & bit) || ls.result[i] == static_cast<std::uint8_t>(svc::LoopResult::None)) continue;
        const bool ok    = (ls.ok_mask & bit) != 0;
        const bool first = !(noticed_ & bit);
        if (!first && (ok == ((noticed_ok_ & bit) != 0) || now - notice_ms_[i] < kNoticeHoldMs)) continue;
        if (ok) {
            bringup::notify(*ctx_, kTag, Severity::Info, "Serial %u loopback PASS", static_cast<unsigned>(i));
        } else {
            bringup::notify(*ctx_, kTag, Severity::Error, "Serial %u loopback FAIL (%s)", static_cast<unsigned>(i),
                   svc::to_string(static_cast<svc::LoopResult>(ls.result[i])));
        }
        notice_ms_[i] = now;
        noticed_      = static_cast<std::uint16_t>(noticed_ | bit);
        noticed_ok_   = static_cast<std::uint16_t>(ok ? (noticed_ok_ | bit) : (noticed_ok_ & ~bit));
    }
    ctx_->bus.publish(svc::Event::make(bringup::kLoopbackStatus, ls));
}

void SerialBringup::publish_status(std::uint64_t now_ms) {
    bringup::BringupStatus bs{};
    bs.uptime_ms    = static_cast<std::uint32_t>(now_ms);
    bs.gnss_state   = gnss_state_;
    bs.gcs_port     = cfg_.gcs_port >= 0 ? static_cast<std::uint8_t>(cfg_.gcs_port) : 0xFF;
    bs.gnss_port    = cfg_.gnss_port >= 0 ? static_cast<std::uint8_t>(cfg_.gnss_port) : 0xFF;
    bs.serial_count = static_cast<std::uint8_t>(cfg_.ports->count());
    if (cfg_.gnss_port >= 0) {
        const svc::SerialPortStats s = cfg_.ports->stats(static_cast<std::size_t>(cfg_.gnss_port));
        bs.gnss_rx_bytes = s.rx_bytes;
        bs.gnss_tx_bytes = s.tx_bytes;
        bs.gnss_baudrate = s.baudrate;
    }
    ctx_->bus.publish(svc::Event::make(bringup::kBringupStatus, bs));
}

void SerialBringup::on_tick(std::uint64_t now_ms) {
    update_gnss(now_ms);
    if (now_ms >= next_report_) {
        next_report_ = now_ms + report_ms_;
        publish_loopback();
    }
    if (now_ms >= next_status_) {
        next_status_ = now_ms + 1000;
        publish_status(now_ms);
    }
}

void SerialBringup::on_event(const svc::Event& e) {
    svc::ParamChanged pc{};
    if (e.id == svc::evt::kParamChanged && e.get(pc)) {
        if (pc.id >= params::kSerPeriodMs && pc.id <= params::kGnssForward) apply_params();
        if (pc.id == params::kGnssBaud && gnss_state_ != GnssState::Absent && cfg_.gnss_port >= 0) {
            cfg_.ports->set_baudrate(static_cast<std::size_t>(cfg_.gnss_port), static_cast<std::uint32_t>(pc.value));
        }
        return;
    }
    bringup::GnssTxRequest req{};
    if (e.id == bringup::kGnssTxRequest && e.get(req)) {
        if (cfg_.gnss_port < 0 || gnss_state_ != GnssState::Connected) {
            bringup::notify(*ctx_, kTag, Severity::Warning, "GNSS: not connected, command dropped");
            return;
        }
        const auto g = static_cast<std::size_t>(cfg_.gnss_port);
        if (req.baudrate != 0) {
            if (core::is_ok(cfg_.ports->set_baudrate(g, req.baudrate))) {
                bringup::notify(*ctx_, kTag, Severity::Notice, "GNSS: port baudrate -> %lu", static_cast<unsigned long>(req.baudrate));
            }
        }
        const std::size_t n = req.len > sizeof(req.data) ? sizeof(req.data) : req.len;
        if (n > 0 && cfg_.ports->write(g, req.data, n) != n) bringup::notify(*ctx_, kTag, Severity::Error, "GNSS: write failed");
    }
}

int SerialBringup::on_command(int argc, char** argv, svc::CommandOutput& out) {
    if (std::strcmp(argv[0], "serial") == 0) {
        if (argc >= 2 && std::strcmp(argv[1], "reset") == 0) {
            for (std::size_t i = 0; i < cfg_.ports->count(); ++i) cfg_.ports->reset_counters(i);
            out.print("counters cleared\r\n");
            return 0;
        }
        static const char* const kRole[] = {"loopback", "gcs", "gnss", "disabled"};
        out.print("port role      mode      last      pass      fail    rx bytes  baud\r\n");
        for (std::size_t i = 0; i < cfg_.ports->count(); ++i) {
            const svc::SerialPortStats s = cfg_.ports->stats(i);
            out.print("%4u %-9s %-9s %-8s %8lu %9lu %11lu  %lu\r\n", static_cast<unsigned>(i),
                      kRole[static_cast<int>(role(i))], svc::to_string(s.mode), svc::to_string(s.last),
                      static_cast<unsigned long>(s.pass), static_cast<unsigned long>(s.fail),
                      static_cast<unsigned long>(s.rx_bytes), static_cast<unsigned long>(s.baudrate));
        }
        return 0;
    }
    // gnss ...
    if (argc >= 3 && std::strcmp(argv[1], "send") == 0) {
        bringup::GnssTxRequest req{};
        std::size_t n = 0;
        for (int a = 2; a < argc; ++a) {   // ghép lại các từ (CommandService đã tách theo khoảng trắng)
            for (const char* c = argv[a]; *c != '\0' && n < sizeof(req.data) - 2; ++c) req.data[n++] = static_cast<std::uint8_t>(*c);
            if (a + 1 < argc && n < sizeof(req.data) - 2) req.data[n++] = ' ';
        }
        req.data[n++] = '\r';
        req.data[n++] = '\n';
        req.len = static_cast<std::uint8_t>(n);
        ctx_->bus.publish(svc::Event::make(bringup::kGnssTxRequest, req));
        out.print("queued %u bytes\r\n", static_cast<unsigned>(n));
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "baud") == 0) {
        bringup::GnssTxRequest req{};
        req.baudrate = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
        ctx_->bus.publish(svc::Event::make(bringup::kGnssTxRequest, req));
        return 0;
    }
    if (cfg_.gnss_port < 0) {
        out.print("no GNSS port\r\n");
        return 0;
    }
    const svc::SerialPortStats s = cfg_.ports->stats(static_cast<std::size_t>(cfg_.gnss_port));
    out.print("port %d state=%s rx=%lu tx=%lu overflow=%lu baud=%lu forward=%d\r\n", cfg_.gnss_port,
              bringup::to_string(gnss_state_.load()), static_cast<unsigned long>(s.rx_bytes),
              static_cast<unsigned long>(s.tx_bytes), static_cast<unsigned long>(s.overflow),
              static_cast<unsigned long>(s.baudrate), forward_.load() ? 1 : 0);
    return 0;
}

}  // namespace app
