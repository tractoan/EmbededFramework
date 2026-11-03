#include "services/drivers/hdlc_imu.hpp"

#include "osal/time.hpp"
#include "utils/crc.hpp"

namespace svc {

core::Status HdlcImu::init(hal::IHdlcPort* port, const Config& cfg) {
    if (cfg.format == nullptr) return core::Status::InvalidArg;
    cfg_  = cfg;   // giữ cấu hình kể cả khi board không có cổng (để báo tên định dạng)
    port_ = port;
    return port == nullptr ? core::Status::NotSupported : core::Status::Ok;
}

core::Status HdlcImu::start() {
    if (port_ == nullptr) return core::Status::NotSupported;
    if (running_) return core::Status::Ok;
    const core::Status st = port_->open(cfg_.port);
    if (!core::is_ok(st)) return st;
    running_ = true;
    return thread_.start(osal::ThreadConfig{cfg_.thread_name, osal::Priority::High, 4096, nullptr},
                         &osal::Thread::invoke<HdlcImu, &HdlcImu::run>, this);
}

void HdlcImu::stop() {
    if (!running_) return;
    running_ = false;
    thread_.join();
    port_->close();
}

bool HdlcImu::latest(ImuSample& out) const {
    osal::LockGuard g(mtx_);
    out = sample_;
    return has_sample_;
}

ImuStats HdlcImu::stats() const {
    ImuStats s;
    s.frames        = frames_.load();
    s.crc_errors    = crc_errors_.load();
    s.format_errors = format_errors_.load();
    s.other_frames  = other_frames_.load();
    s.rate_hz       = rate_hz_.load();
    s.last_frame_ms = last_frame_ms_.load();
    return s;
}

void HdlcImu::process_frame(const std::uint8_t* frame, std::size_t len, const hal::HdlcFrameStatus& st,
                            std::uint64_t now_ms) {
    if (window_start_ == 0) window_start_ = now_ms;
    if (now_ms - window_start_ >= 1000) {
        rate_hz_       = static_cast<std::uint32_t>(window_frames_ * 1000ull / (now_ms - window_start_));
        window_frames_ = 0;
        window_start_  = now_ms;
    }
    if (len == 0) return;   // chỉ cập nhật cửa sổ đo tần số

    if (st.crc_error || st.aborted || st.overrun) {
        crc_errors_.fetch_add(1);
        return;
    }
    if (cfg_.software_fcs) {
        if (len < 3) {
            format_errors_.fetch_add(1);
            return;
        }
        const std::uint16_t fcs = utils::crc16_x25(frame, len - 2);
        if (frame[len - 2] != (fcs & 0xFF) || frame[len - 1] != (fcs >> 8)) {
            crc_errors_.fetch_add(1);
            return;
        }
        len -= 2;
    }

    ImuSample s;
    s.time_ms = static_cast<std::uint32_t>(now_ms);
    switch (decode_imu_frame(*cfg_.format, frame, len, s)) {
        case ImuDecodeResult::Ok: break;
        case ImuDecodeResult::NotDataMessage: other_frames_.fetch_add(1); return;
        default: format_errors_.fetch_add(1); return;
    }
    {
        osal::LockGuard g(mtx_);
        sample_     = s;
        has_sample_ = true;
    }
    frames_.fetch_add(1);
    ++window_frames_;
    last_frame_ms_ = now_ms;
}

void HdlcImu::run() {
    std::uint8_t buf[256];
    while (running_) {
        hal::HdlcFrameStatus st{};
        const std::size_t n = port_->read_frame(buf, sizeof(buf), 100, &st);
        process_frame(buf, n, st, osal::now_ms());
    }
}

}  // namespace svc
