#pragma once
// Bring-up IMU (chính sách): gửi mẫu mới nhất + thống kê với tần số imu.rate_hz (kImuSample), phát hiện mất
// dữ liệu (imu.timeout_ms) và báo người vận hành. Việc đọc khung chạy ở svc::SensorScheduler (thread riêng,
// do product khai báo); module chỉ đọc msg::Topic<ImuSample> + IImu::stats() – không chặn, không thread.
#include <array>

#include "app/module.hpp"
#include "app_params.hpp"
#include "bringup_events.hpp"
#include "device/imu.hpp"
#include "messaging/topic.hpp"

namespace app {

class ImuBringup final : public IModule, public svc::ICommandHandler {
public:
    static constexpr std::size_t kMaxImu = 4;

    struct Channel {
        device::IImu* imu                              = nullptr;   // tên định dạng, trạng thái cổng, thống kê
        const msg::Topic<device::ImuSample>* samples   = nullptr;   // mẫu mới nhất (do nhóm lấy mẫu publish)
    };

    ImuBringup(const Channel* channels, std::size_t count);

    const char* name() const override { return "imu_bringup"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_event(const msg::Event& e) override;
    std::uint32_t tick_period_ms() const override { return period_ms_; }
    void on_tick(std::uint64_t now_ms) override;

    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "imu"

private:
    std::array<Channel, kMaxImu> ch_{};
    std::size_t count_ = 0;
    std::array<bool, kMaxImu> start_failed_{};
    enum class Link : std::uint8_t { Unknown, Alive, Lost };   // Unknown: chưa hết thời gian chờ sau khởi động
    std::array<Link, kMaxImu> link_{};
    std::uint64_t start_ms_ = 0;
    SystemContext* ctx_ = nullptr;
    std::uint32_t period_ms_  = 100;
    std::uint32_t timeout_ms_ = 500;

    const svc::ParamDef defs_[2] = {
        {params::kImuRateHz, "imu.rate_hz", 10, 1, 100, "Hz"},
        {params::kImuTimeoutMs, "imu.timeout_ms", 500, 50, 10000, "ms"},
    };
};

}  // namespace app
