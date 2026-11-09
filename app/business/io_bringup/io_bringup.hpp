#pragma once
// Bring-up cổng số: đọc DI theo chu kỳ 20 ms, báo kIoStatus khi thay đổi và định kỳ io.report_ms;
// đặt DO theo yêu cầu kOutputSetRequest (từ phần mềm giao diện hoặc lệnh CLI "io").
#include <atomic>

#include "app/module.hpp"
#include "app_params.hpp"
#include "bringup_events.hpp"
#include "device/digital_io.hpp"

namespace app {

class IoBringup final : public IModule, public svc::ICommandHandler {
public:
    IoBringup(device::IDigitalInputs* inputs, device::IDigitalOutputs* outputs) : in_(inputs), out_(outputs) {}

    const char* name() const override { return "io_bringup"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_event(const msg::Event& e) override;
    std::uint32_t tick_period_ms() const override { return 20; }
    void on_tick(std::uint64_t now_ms) override;

    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "io"

private:
    void publish(std::uint64_t now_ms);

    device::IDigitalInputs* in_;
    device::IDigitalOutputs* out_;
    SystemContext* ctx_ = nullptr;
    std::uint32_t report_ms_ = 200;
    std::uint64_t next_report_ = 0;
    std::uint64_t now_ms_      = 0;   // thời điểm tick gần nhất (một nguồn thời gian duy nhất)
    std::atomic<std::uint32_t> inputs_{0};
    std::uint32_t changes_ = 0;
    bool first_ = true;

    const svc::ParamDef def_{params::kIoReportMs, "io.report_ms", 200, 20, 10000, "ms"};
};

}  // namespace app
