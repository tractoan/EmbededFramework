#include "device/hdlc_imu.hpp"

#include "osal/time.hpp"
#include "utils/crc.hpp"

namespace device {

core::Status HdlcImu::init(hal::IHdlcPort* port, const Config& cfg) {
    if (cfg.format == nullptr) return core::Status::InvalidArg;
    cfg_    = cfg;
    port_   = port;
    status_ = core::Status::NotSupported;
    return port == nullptr ? core::Status::NotSupported : core::Status::Ok;
}

core::Status HdlcImu::open() {
    const core::Status st = port_ != nullptr ? port_->open(cfg_.port) : core::Status::NotSupported;
    status_ = st;
    return st;
}

void HdlcImu::close() {
    if (port_ != nullptr && core::is_ok(status_.load())) port_->close();
    status_ = core::Status::NotSupported;
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

bool HdlcImu::process_frame(const std::uint8_t* frame, std::size_t len, const hal::HdlcFrameStatus& st,
                            std::uint64_t now_ms, ImuSample& out) {
    if (window_start_ == 0) window_start_ = now_ms;
    if (now_ms >= window_start_ + 1000) {
        rate_hz_       = static_cast<std::uint32_t>(window_frames_ * 1000ull / (now_ms - window_start_));
        window_frames_ = 0;
        window_start_  = now_ms;
    }
    if (len == 0) return false;

    if (st.crc_error || st.aborted || st.overrun) {
        crc_errors_.fetch_add(1);
        return false;
    }
    if (cfg_.software_fcs) {
        if (len < 3) {
            format_errors_.fetch_add(1);
            return false;
        }
        const std::uint16_t fcs = utils::crc16_x25(frame, len - 2);
        if (frame[len - 2] != (fcs & 0xFF) || frame[len - 1] != (fcs >> 8)) {
            crc_errors_.fetch_add(1);
            return false;
        }
        len -= 2;
    }

    ImuSample s{};
    s.time_ms = static_cast<std::uint32_t>(now_ms);
    switch (decode_imu_frame(*cfg_.format, frame, len, s)) {
        case ImuDecodeResult::Ok: break;
        case ImuDecodeResult::NotDataMessage: other_frames_.fetch_add(1); return false;
        default: format_errors_.fetch_add(1); return false;
    }
    out = s;
    frames_.fetch_add(1);
    ++window_frames_;
    last_frame_ms_ = now_ms;
    return true;
}

core::Status HdlcImu::read(ImuSample& out, std::uint32_t timeout_ms) {
    const core::Status st = status_.load();
    if (!core::is_ok(st)) return st;   // cổng chưa mở được: SensorScheduler tính là lỗi, nghỉ tới hết timeout
    std::uint8_t buf[256];
    const std::uint64_t deadline = osal::now_ms() + timeout_ms;
    for (;;) {
        const std::uint64_t now  = osal::now_ms();
        const std::uint32_t left = now < deadline ? static_cast<std::uint32_t>(deadline - now) : 0u;
        hal::HdlcFrameStatus fs{};
        const std::size_t n = port_->read_frame(buf, sizeof(buf), left, &fs);
        if (process_frame(buf, n, fs, osal::now_ms(), out)) return core::Status::Ok;
        // Khung lỗi / không phải dữ liệu: đọc tiếp tới hết timeout. 0 byte = đã chờ hết thời gian còn lại.
        if (n == 0 || left == 0) return core::Status::Timeout;
    }
}

}  // namespace device
