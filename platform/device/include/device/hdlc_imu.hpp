#pragma once
// Driver IMU giao tiếp SDLC/HDLC (không thread): đọc từng khung từ hal::IHdlcPort, kiểm FCS (tùy chọn),
// giải mã theo ImuFormat, thống kê khung/lỗi/tần số. Bên gọi read() liên tục (thường là một nhóm
// svc::SensorScheduler chế độ Blocking, thread riêng priority cao) – xem products/hw_bringup.
#include <atomic>

#include "core/noncopyable.hpp"
#include "device/imu.hpp"
#include "hal/hdlc.hpp"

namespace device {

class HdlcImu final : public IImu, private core::NonCopyable {
public:
    struct Config {
        const ImuFormat* format = nullptr;
        hal::HdlcConfig port{};
        bool software_fcs       = false;   // true: khung còn 2 byte FCS-16 ở cuối, kiểm tra bằng phần mềm
    };

    // Lưu cấu hình (kể cả khi board không có cổng – để báo tên định dạng). NotSupported nếu port == nullptr.
    core::Status init(hal::IHdlcPort* port, const Config& cfg);
    core::Status open();    // mở + cấu hình cổng HDLC (lúc khởi động); kết quả giữ ở status()
    void close();

    const char* format_name() const override { return cfg_.format ? cfg_.format->name : "?"; }
    std::uint8_t type_id() const override { return cfg_.format ? cfg_.format->type_id : 0; }
    core::Status status() const override { return status_.load(); }
    core::Status read(ImuSample& out, std::uint32_t timeout_ms) override;
    ImuStats stats() const override;

    // Xử lý một khung (public để test không cần cổng). len == 0: chỉ cập nhật cửa sổ đo tần số.
    // true nếu khung là mẫu dữ liệu hợp lệ (đã ghi vào out).
    bool process_frame(const std::uint8_t* frame, std::size_t len, const hal::HdlcFrameStatus& st,
                       std::uint64_t now_ms, ImuSample& out);

private:
    hal::IHdlcPort* port_ = nullptr;
    Config cfg_{};
    std::atomic<core::Status> status_{core::Status::NotSupported};

    // Thống kê: ghi bởi bên gọi read(), đọc từ thread khác qua stats()
    std::atomic<std::uint32_t> frames_{0}, crc_errors_{0}, format_errors_{0}, other_frames_{0}, rate_hz_{0};
    std::atomic<std::uint64_t> last_frame_ms_{0};
    std::uint64_t window_start_  = 0;
    std::uint32_t window_frames_ = 0;
};

}  // namespace device
