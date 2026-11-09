#pragma once
// Bring-up máy thu GNSS (CHÍNH SÁCH; cơ chế đọc/giải mã/gửi lệnh nằm ở svc::GnssReceiver):
//  - trong gnss.detect_ms (mặc định 5 s) sau khởi động không nhận được byte nào -> coi như không có máy thu:
//    dừng receiver, chuyển cổng sang kiểm tra loopback (SerialBringup báo kết quả như các cổng khác)
//  - có máy thu: phát kGnssFix mỗi fix mới, chuyển tiếp dữ liệu thô (kGnssRxData, nếu gnss.forward),
//    nhận lệnh cấu hình kGnssTxRequest (chỉ khi đã kết nối), báo BringupStatus 1 Hz
#include <atomic>

#include "app/module.hpp"
#include "app_params.hpp"
#include "bringup_events.hpp"
#include "services/gnss_receiver.hpp"
#include "services/serial_port_bank.hpp"

namespace app {

class GnssBringup final : public IModule, public svc::ICommandHandler {
public:
    struct Config {
        svc::IGnssReceiver* receiver = nullptr;
        svc::ISerialPorts* ports     = nullptr;   // để đổi cổng sang loopback và báo số cổng
        int gnss_port                = -1;
        int gcs_port                 = -1;        // chỉ để báo cáo (BringupStatus)
    };

    explicit GnssBringup(const Config& cfg) : cfg_(cfg) {}

    const char* name() const override { return "gnss_bringup"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_stop() override;
    void on_event(const msg::Event& e) override;
    std::uint32_t tick_period_ms() const override { return 20; }
    void on_tick(std::uint64_t now_ms) override;

    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "gnss"

    bringup::GnssState state() const { return state_; }

private:
    void apply_params();
    void publish_status(std::uint64_t now_ms);
    void forward_raw();

    Config cfg_;
    SystemContext* ctx_ = nullptr;
    std::uint32_t detect_ms_ = 5000;
    std::atomic<bool> forward_{true};
    std::atomic<bringup::GnssState> state_{bringup::GnssState::Detecting};   // CLI (thread kênh) đọc
    std::atomic<std::uint32_t> raw_dropped_{0};   // gói thô bỏ do hết pool buffer
    std::uint64_t start_ms_ = 0, next_status_ = 0;
    std::uint32_t last_fix_seq_ = 0;

    const svc::ParamDef defs_[3] = {
        {params::kGnssDetectMs, "gnss.detect_ms", 5000, 1000, 60000, "ms"},
        {params::kGnssForward, "gnss.forward", 1, 0, 1, "0/1"},
        {params::kGnssBaud, "gnss.baud", 115200, 1200, 921600, "bps"},
    };
};

}  // namespace app
