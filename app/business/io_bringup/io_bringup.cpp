#include "io_bringup/io_bringup.hpp"

#include <cstdlib>
#include <cstring>

#include "bringup_notify.hpp"
#include "osal/time.hpp"
#include "services/logger.hpp"

namespace app {

core::Status IoBringup::on_init(SystemContext& ctx) {
    ctx_ = &ctx;
    if ((in_ != nullptr && in_->count() > 32) || (out_ != nullptr && out_->count() > 32)) return core::Status::InvalidArg;
    core::Status st = ctx.params.register_params(&def_, 1);
    if (core::is_ok(st)) st = ctx.cli.register_command({"io", "[set <ch> <0|1> | out <hex> | all <0|1>] - digital I/O", this});
    return st;
}

core::Status IoBringup::on_start() {
    report_ms_ = static_cast<std::uint32_t>(ctx_->params.get_or(params::kIoReportMs, def_.default_value));
    if (out_ != nullptr && out_->count() > 0) {
        const std::uint32_t all = out_->count() >= 32 ? 0xFFFFFFFFu : ((1u << out_->count()) - 1u);
        out_->write(all, 0);   // trạng thái an toàn: mọi output tắt
    }
    return core::Status::Ok;
}

void IoBringup::publish(std::uint64_t now_ms) {
    bringup::IoStatus s{};
    s.time_ms      = static_cast<std::uint32_t>(now_ms);
    s.inputs       = inputs_.load();
    s.outputs      = out_ ? out_->state() : 0;
    s.input_count  = static_cast<std::uint8_t>(in_ ? in_->count() : 0);
    s.output_count = static_cast<std::uint8_t>(out_ ? out_->count() : 0);
    ctx_->bus.publish(msg::Event::make(bringup::kIoStatus, s));
    next_report_ = now_ms + report_ms_;
}

void IoBringup::on_tick(std::uint64_t now_ms) {
    now_ms_ = now_ms;
    const std::uint32_t v = in_ ? in_->read() : 0;
    const bool changed    = first_ || v != inputs_.load();
    if (changed && !first_) {
        ++changes_;
        LOGD("io", "inputs %08lX -> %08lX", static_cast<unsigned long>(inputs_.load()), static_cast<unsigned long>(v));
    }
    first_  = false;
    inputs_ = v;
    if (changed || now_ms >= next_report_) publish(now_ms);
}

void IoBringup::on_event(const msg::Event& e) {
    svc::ParamChanged pc{};
    if (e.id == svc::evt::kParamChanged && e.get(pc) && pc.id == params::kIoReportMs) {
        report_ms_ = static_cast<std::uint32_t>(pc.value);
        return;
    }
    bringup::OutputSetRequest req{};
    if (e.id != bringup::kOutputSetRequest || !e.get(req)) return;
    if (out_ == nullptr) return;
    const core::Status st = out_->write(req.mask, req.value);
    if (!core::is_ok(st)) {
        bringup::notify(*ctx_, "io", bringup::Severity::Error, "output set %08lX failed: %s",
                                               static_cast<unsigned long>(req.mask), core::to_string(st));
    }
    publish(now_ms_ ? now_ms_ : osal::now_ms());   // phản hồi ngay trạng thái mới
}

int IoBringup::on_command(int argc, char** argv, svc::CommandOutput& out) {
    // Lệnh chạy trên thread của kênh -> chỉ publish yêu cầu, module áp dụng trên thread ứng dụng.
    bringup::OutputSetRequest req{};
    bool request = false;
    if (argc >= 4 && std::strcmp(argv[1], "set") == 0) {
        const long ch = std::strtol(argv[2], nullptr, 10);
        if (out_ == nullptr || ch < 0 || static_cast<std::size_t>(ch) >= out_->count()) {
            out.print("channel out of range\r\n");
            return -1;
        }
        req.mask  = 1u << ch;
        req.value = std::atoi(argv[3]) ? req.mask : 0;
        request   = true;
    } else if (argc >= 3 && std::strcmp(argv[1], "out") == 0) {
        req.mask  = out_ && out_->count() < 32 ? (1u << out_->count()) - 1u : 0xFFFFFFFFu;
        req.value = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 16));
        request   = true;
    } else if (argc >= 3 && std::strcmp(argv[1], "all") == 0) {
        req.mask  = out_ && out_->count() < 32 ? (1u << out_->count()) - 1u : 0xFFFFFFFFu;
        req.value = std::atoi(argv[2]) ? req.mask : 0;
        request   = true;
    }
    if (request) {
        ctx_->bus.publish(msg::Event::make(bringup::kOutputSetRequest, req));
        return 0;
    }
    const std::uint32_t in = inputs_.load(), o = out_ ? out_->state() : 0;
    const std::size_t ni = in_ ? in_->count() : 0, no = out_ ? out_->count() : 0;
    out.print("DI (%u): ", static_cast<unsigned>(ni));
    for (std::size_t i = 0; i < ni; ++i) out.print("%c", (in >> i) & 1u ? '1' : '0');
    out.print("  [0..%u]\r\nDO (%u): ", static_cast<unsigned>(ni ? ni - 1 : 0), static_cast<unsigned>(no));
    for (std::size_t i = 0; i < no; ++i) out.print("%c", (o >> i) & 1u ? '1' : '0');
    out.print("\r\n");
    return 0;
}

}  // namespace app
