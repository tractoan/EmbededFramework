#pragma once
// Bring-up dãy cổng serial:
//  - 1 cổng dành cho phần mềm giao diện (do kênh MAVLink sở hữu, chỉ báo vai trò)
//  - 1 cổng cho máy thu GNSS: trong gnss.detect_ms (mặc định 5 s) sau khởi động nếu không nhận
//    được byte nào -> coi như không có máy thu, chuyển cổng sang kiểm tra loopback.
//    Có máy thu: chuyển tiếp dữ liệu lên giao diện (kGnssRxData), nhận lệnh cấu hình (kGnssTxRequest).
//    Dữ liệu máy thu được giải mã NMEA (GGA/RMC) -> kGnssFix (giờ/ngày UTC, lat, lon, độ cao, số vệ tinh).
//  - Các cổng còn lại: kiểm tra loopback liên tục, báo kLoopbackStatus.
#include "app/module.hpp"
#include "app_params.hpp"
#include "bringup_events.hpp"
#include "device/nmea.hpp"
#include "services/serial_port_bank.hpp"

namespace app {

class SerialBringup final : public IModule, public svc::ICommandHandler, private IWorkerHandler {
public:
    struct Config {
        svc::ISerialPorts* ports = nullptr;
        int gcs_port             = -1;   // -1: không có
        int gnss_port            = -1;   // -1: không có
        // Thread ghi lệnh tới máy thu GNSS (ghi UART có thể chặn). Ghi đè được bằng ModuleManager::add(.., &task).
        osal::ThreadConfig gnss_tx_task{"gnss_tx", osal::Priority::Normal, 2048, nullptr};
    };

    explicit SerialBringup(const Config& cfg) : cfg_(cfg), worker_(*this, cfg.gnss_tx_task) {}

    const char* name() const override { return "serial_bringup"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_stop() override;
    void on_event(const msg::Event& e) override;
    std::uint32_t tick_period_ms() const override { return 20; }
    void on_tick(std::uint64_t now_ms) override;

    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "serial", "gnss"
    ModuleWorkerBase* worker() override { return &worker_; }

    bringup::GnssState gnss_state() const { return gnss_state_; }

private:
    void apply_params();
    void update_gnss(std::uint64_t now_ms);
    void publish_loopback();
    void publish_status(std::uint64_t now_ms);
    void on_nmea(const device::GnssFix& fix, device::NmeaParser::Result kind, std::uint64_t now_ms);
    bringup::SerialRole role(std::size_t port) const;

    Config cfg_;
    SystemContext* ctx_ = nullptr;
    std::uint32_t detect_ms_ = 5000, report_ms_ = 500;
    std::atomic<bool> forward_{true};
    std::atomic<std::uint32_t> gnss_dropped_{0};   // gói GNSS bỏ do hết pool buffer

    // Giải mã NMEA (thread ứng dụng). Bản sao fix cuối dạng atomic cho lệnh CLI "gnss" (thread kênh).
    device::NmeaParser nmea_;
    std::uint64_t last_gga_ms_ = 0;
    std::atomic<std::uint32_t> fix_time_ms_{0}, fix_count_{0}, nmea_errors_{0};
    std::atomic<std::int32_t> fix_lat_{0}, fix_lon_{0};
    std::atomic<std::uint8_t> fix_flags_{0}, fix_sats_{0}, fix_quality_{0};
    std::uint64_t start_ms_ = 0, next_report_ = 0, next_status_ = 0;
    // atomic: CLI (thread kênh) đọc
    std::atomic<bringup::GnssState> gnss_state_{bringup::GnssState::Detecting};
    // Thông báo PASS<->FAIL: trạng thái đã báo + thời điểm báo gần nhất của từng cổng (chống "spam" khi chập chờn)
    static constexpr std::uint32_t kNoticeHoldMs = 5000;
    std::uint16_t noticed_ok_ = 0, noticed_ = 0;
    std::uint64_t notice_ms_[bringup::kMaxSerial]{};

    const svc::ParamDef defs_[6] = {
        {params::kSerPeriodMs, "ser.period_ms", 200, 20, 10000, "ms"},
        {params::kSerTimeoutMs, "ser.timeout_ms", 50, 5, 2000, "ms"},
        {params::kSerReportMs, "ser.report_ms", 500, 100, 10000, "ms"},
        {params::kGnssDetectMs, "gnss.detect_ms", 5000, 1000, 60000, "ms"},
        {params::kGnssForward, "gnss.forward", 1, 0, 1, "0/1"},
        {params::kGnssBaud, "gnss.baud", 115200, 1200, 921600, "bps"},
    };

    void on_job(const msg::Event& job) override;   // thread worker: ghi lệnh/đổi baud cổng GNSS

    ModuleWorker<8> worker_;   // khai báo CUỐI: hủy đầu tiên -> thread dừng trước dữ liệu nó dùng
};

}  // namespace app
