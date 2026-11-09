#pragma once
// Bring-up dãy cổng serial (kiểm tra loopback):
//  - cổng ở chế độ External = cổng của phần mềm giao diện (kênh MAVLink sở hữu) -> chỉ báo vai trò
//  - cổng ở chế độ Stream = cổng máy thu GNSS (do GnssBringup + svc::GnssReceiver quản lý) -> chỉ báo vai trò;
//    khi không có máy thu, GnssBringup chuyển cổng đó sang Loopback và nó được thử như các cổng khác
//  - các cổng Loopback: thử liên tục, báo kLoopbackStatus, thông báo khi một cổng đổi PASS <-> FAIL
#include "app/module.hpp"
#include "app_params.hpp"
#include "bringup_events.hpp"
#include "services/serial_port_bank.hpp"

namespace app {

class SerialBringup final : public IModule, public svc::ICommandHandler {
public:
    struct Config {
        svc::ISerialPorts* ports = nullptr;
    };

    explicit SerialBringup(const Config& cfg) : cfg_(cfg) {}

    const char* name() const override { return "serial_bringup"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_stop() override;
    void on_event(const msg::Event& e) override;
    std::uint32_t tick_period_ms() const override { return report_ms_; }
    void on_tick(std::uint64_t now_ms) override;

    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "serial"

    static bringup::SerialRole role_of(svc::SerialMode m);

private:
    void apply_params();
    void publish_loopback(std::uint64_t now_ms);

    Config cfg_;
    SystemContext* ctx_ = nullptr;
    std::uint32_t report_ms_ = 500;
    // Thông báo PASS<->FAIL: trạng thái đã báo + thời điểm báo gần nhất của từng cổng (chống "spam" khi chập chờn)
    static constexpr std::uint32_t kNoticeHoldMs = 5000;
    std::uint16_t noticed_ok_ = 0, noticed_ = 0;
    std::uint64_t notice_ms_[bringup::kMaxSerial]{};

    const svc::ParamDef defs_[3] = {
        {params::kSerPeriodMs, "ser.period_ms", 200, 20, 10000, "ms"},
        {params::kSerTimeoutMs, "ser.timeout_ms", 50, 5, 2000, "ms"},
        {params::kSerReportMs, "ser.report_ms", 500, 100, 10000, "ms"},
    };
};

}  // namespace app
