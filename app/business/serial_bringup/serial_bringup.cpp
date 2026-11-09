#include "serial_bringup/serial_bringup.hpp"

#include <cstring>

#include "bringup_notify.hpp"
#include "osal/time.hpp"

namespace app {

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
    return st;
}

void SerialBringup::apply_params() {
    const auto get = [&](svc::ParamId id, std::int32_t def) { return ctx_->params.get_or(id, def); };
    cfg_.ports->set_test_timing(static_cast<std::uint32_t>(get(params::kSerPeriodMs, 200)),
                                static_cast<std::uint32_t>(get(params::kSerTimeoutMs, 50)));
    report_ms_ = static_cast<std::uint32_t>(get(params::kSerReportMs, 500));
}

core::Status SerialBringup::on_start() {
    apply_params();
    return cfg_.ports->start();
}

void SerialBringup::on_stop() { cfg_.ports->stop(); }

SerialRole SerialBringup::role_of(svc::SerialMode m) {
    switch (m) {
        case svc::SerialMode::External: return SerialRole::Gcs;
        case svc::SerialMode::Stream:   return SerialRole::Gnss;
        case svc::SerialMode::Loopback: return SerialRole::Loopback;
        default:                        return SerialRole::Disabled;
    }
}

void SerialBringup::publish_loopback(std::uint64_t now) {
    bringup::LoopbackHeader ls{};
    bringup::PortResult ports[bringup::kMaxSerial]{};
    ls.count = static_cast<std::uint8_t>(cfg_.ports->count());
    for (std::size_t i = 0; i < ls.count; ++i) {
        const svc::SerialPortStats s = cfg_.ports->stats(i);
        ports[i].role   = static_cast<std::uint8_t>(role_of(s.mode));
        ports[i].result = static_cast<std::uint8_t>(s.last);
        ports[i].pass   = s.pass;
        ports[i].fail   = s.fail;
        if (s.mode == svc::SerialMode::Loopback) ls.tested_mask = static_cast<std::uint16_t>(ls.tested_mask | (1u << i));
        if (s.mode == svc::SerialMode::Loopback && s.last == svc::LoopResult::Pass) {
            ls.ok_mask = static_cast<std::uint16_t>(ls.ok_mask | (1u << i));
        }
    }
    // Báo người vận hành khi một cổng đổi PASS <-> FAIL. Cổng chập chờn: tối đa 1 thông báo / kNoticeHoldMs
    // (trạng thái chi tiết luôn có trong LoopbackStatus).
    for (std::size_t i = 0; i < ls.count; ++i) {
        const auto bit = static_cast<std::uint16_t>(1u << i);
        if (!(ls.tested_mask & bit) || ports[i].result == static_cast<std::uint8_t>(svc::LoopResult::None)) continue;
        const bool ok    = (ls.ok_mask & bit) != 0;
        const bool first = !(noticed_ & bit);
        if (!first && (ok == ((noticed_ok_ & bit) != 0) || now < notice_ms_[i] + kNoticeHoldMs)) continue;
        if (ok) {
            bringup::notify(*ctx_, kTag, Severity::Info, "Serial %u loopback PASS", static_cast<unsigned>(i));
        } else {
            bringup::notify(*ctx_, kTag, Severity::Error, "Serial %u loopback FAIL (%s)", static_cast<unsigned>(i),
                            svc::to_string(static_cast<svc::LoopResult>(ports[i].result)));
        }
        notice_ms_[i] = now;
        noticed_      = static_cast<std::uint16_t>(noticed_ | bit);
        noticed_ok_   = static_cast<std::uint16_t>(ok ? (noticed_ok_ | bit) : (noticed_ok_ & ~bit));
    }
    ctx_->bus.publish(msg::Event::make(bringup::kLoopbackStatus, ls, msg::Buffer::copy_of(ports, ls.count * sizeof(ports[0]))));
}

void SerialBringup::on_tick(std::uint64_t now_ms) { publish_loopback(now_ms); }

void SerialBringup::on_event(const msg::Event& e) {
    svc::ParamChanged pc{};
    if (e.id == svc::evt::kParamChanged && e.get(pc) && pc.id >= params::kSerPeriodMs && pc.id <= params::kSerReportMs) {
        apply_params();   // ser.report_ms: chu kỳ tick đọc lại ở lần tick kế tiếp
    }
}

int SerialBringup::on_command(int argc, char** argv, svc::CommandOutput& out) {
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
                  kRole[static_cast<int>(role_of(s.mode))], svc::to_string(s.mode), svc::to_string(s.last),
                  static_cast<unsigned long>(s.pass), static_cast<unsigned long>(s.fail),
                  static_cast<unsigned long>(s.rx_bytes), static_cast<unsigned long>(s.baudrate));
    }
    return 0;
}

}  // namespace app
