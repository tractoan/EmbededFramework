#pragma once
// Driver IMU giao tiếp SDLC/HDLC: thread riêng đọc từng khung từ hal::IHdlcPort,
// giải mã theo ImuFormat, giữ mẫu mới nhất + thống kê (thread-safe).
#include <atomic>

#include "core/noncopyable.hpp"
#include "hal/hdlc.hpp"
#include "osal/mutex.hpp"
#include "osal/thread.hpp"
#include "services/drivers/imu.hpp"

namespace svc {

class HdlcImu final : public IImu, private core::NonCopyable {
public:
    struct Config {
        const ImuFormat* format = nullptr;
        hal::HdlcConfig port{};
        bool software_fcs       = false;   // true: khung còn 2 byte FCS-16 ở cuối, kiểm tra bằng phần mềm
        const char* thread_name = "imu";
    };

    core::Status init(hal::IHdlcPort* port, const Config& cfg);

    const char* format_name() const override { return cfg_.format ? cfg_.format->name : "?"; }
    std::uint8_t type_id() const override { return cfg_.format ? cfg_.format->type_id : 0; }
    core::Status start() override;
    void stop() override;
    bool latest(ImuSample& out) const override;
    ImuStats stats() const override;

    void process_frame(const std::uint8_t* frame, std::size_t len, const hal::HdlcFrameStatus& st,
                       std::uint64_t now_ms);   // public để test không cần thread

private:
    void run();

    hal::IHdlcPort* port_ = nullptr;
    Config cfg_{};
    osal::Thread thread_;
    std::atomic<bool> running_{false};

    mutable osal::Mutex mtx_;
    ImuSample sample_{};
    bool has_sample_ = false;

    std::atomic<std::uint32_t> frames_{0}, crc_errors_{0}, format_errors_{0}, other_frames_{0}, rate_hz_{0};
    std::atomic<std::uint64_t> last_frame_ms_{0};
    std::uint64_t window_start_ = 0;
    std::uint32_t window_frames_ = 0;
};

}  // namespace svc
