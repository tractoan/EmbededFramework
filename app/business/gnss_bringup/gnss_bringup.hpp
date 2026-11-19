#pragma once
// Bring-up máy thu GNSS (CHÍNH SÁCH). Cơ chế dùng chung của framework:
//   cổng Stream của SerialPortBank -> svc::SerialStreamPort (IUart, tap byte thô)
//     -> svc::StreamSensorTask<device::NmeaFixParser, GnssFix> (nhóm SensorScheduler riêng) -> Topic<GnssFix>
//  - trong gnss.detect_ms (mặc định 5 s) sau khởi động cổng không nhận được byte nào -> coi như không có máy thu:
//    chuyển cổng sang kiểm tra loopback (SerialBringup báo như các cổng khác; task cảm biến tự ngừng đọc)
//  - có máy thu: phát kGnssFix mỗi fix mới (theo số thứ tự của Topic), chuyển tiếp dữ liệu thô (kGnssRxData,
//    nếu gnss.forward), nhận lệnh cấu hình kGnssTxRequest (chỉ khi đã kết nối) -> worker "gnss_tx" ghi cổng /
//    đổi baud (ghi UART có thể chặn: không làm trên thread ứng dụng), báo BringupStatus 1 Hz
#include <atomic>

#include "app/module.hpp"
#include "app_params.hpp"
#include "bringup_events.hpp"
#include "device/nmea.hpp"
#include "messaging/topic.hpp"
#include "services/serial_port_bank.hpp"
#include "services/serial_stream_port.hpp"

namespace app {

class GnssBringup final : public IModule, public svc::ICommandHandler, private IWorkerHandler {
public:
    struct Config {
        svc::ISerialPorts* ports                  = nullptr;   // đổi chế độ/baud, ghi lệnh, thống kê byte
        int gnss_port                             = -1;
        int gcs_port                              = -1;        // chỉ để báo cáo (BringupStatus)
        svc::SerialStreamPort* stream             = nullptr;   // tap byte thô của cổng GNSS
        const msg::Topic<device::GnssFix>* fixes  = nullptr;   // fix do nhóm cảm biến GNSS publish
        const device::NmeaFixParser* nmea         = nullptr;   // bộ đếm câu / lỗi checksum (chỉ đọc)
    };

    explicit GnssBringup(const Config& cfg) : cfg_(cfg) {}

    const char* name() const override { return "gnss_bringup"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_stop() override;
    void on_event(const msg::Event& e) override;
    std::uint32_t tick_period_ms() const override { return 20; }
    void on_tick(std::uint64_t now_ms) override;
    ModuleWorkerBase* worker() override { return &worker_; }

    int on_command(int argc, char** argv, svc::CommandReply& out) override;   // "gnss"

    bringup::GnssState state() const { return state_; }

private:
    void apply_params();
    void publish_status(std::uint64_t now_ms);
    void forward_raw();
    void on_job(const msg::Event& job) override;   // worker "gnss_tx": ghi lệnh / đổi baud

    Config cfg_;
    SystemContext* ctx_ = nullptr;
    std::uint32_t detect_ms_ = 5000;
    std::atomic<bool> forward_{true};
    std::atomic<bringup::GnssState> state_{bringup::GnssState::Detecting};   // CLI (thread kênh) đọc
    std::atomic<std::uint32_t> raw_dropped_{0};   // gói thô bỏ do hết pool buffer
    std::uint64_t start_ms_ = 0, next_status_ = 0;
    std::uint32_t last_fix_seq_ = 0;

    std::atomic<std::uint32_t> tx_dropped_{0};    // lệnh bỏ do hàng đợi worker đầy
    std::atomic<std::uint32_t> tx_errors_{0};     // ghi không đủ byte (worker)

    const svc::ParamDef defs_[3] = {
        {params::kGnssDetectMs, "gnss.detect_ms", 5000, 1000, 60000, "ms"},
        {params::kGnssForward, "gnss.forward", 1, 0, 1, "0/1"},
        {params::kGnssBaud, "gnss.baud", 115200, 1200, 921600, "bps"},
    };
    ModuleWorker<8> worker_{*this, {"gnss_tx", osal::Priority{10}, 2048, nullptr}};   // khai báo CUỐI
};

}  // namespace app
