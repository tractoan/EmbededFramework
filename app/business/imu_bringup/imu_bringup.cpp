#include "imu_bringup/imu_bringup.hpp"

#include <cmath>
#include <cstring>

#include "bringup_notify.hpp"
#include "osal/time.hpp"
#include "services/logger.hpp"

namespace app {

using bringup::Severity;

ImuBringup::ImuBringup(const Channel* channels, std::size_t count) {
    for (std::size_t i = 0; i < count && i < kMaxImu; ++i) {
        if (channels[i].imu != nullptr && channels[i].samples != nullptr) ch_[count_++] = channels[i];
    }
}

core::Status ImuBringup::on_init(SystemContext& ctx) {
    ctx_ = &ctx;
    core::Status st = ctx.params.register_params(defs_, 2);
    if (core::is_ok(st)) st = ctx.cli.register_command({"imu", "- IMU frame statistics and last sample", this});
    return st;
}

core::Status ImuBringup::on_start() {
    period_ms_  = 1000u / static_cast<std::uint32_t>(ctx_->params.get_or(params::kImuRateHz, 10));
    timeout_ms_ = static_cast<std::uint32_t>(ctx_->params.get_or(params::kImuTimeoutMs, 500));
    start_ms_   = osal::now_ms();
    for (std::size_t i = 0; i < count_; ++i) {
        // Cổng được mở lúc product::setup(); không mở được -> báo, không dừng hệ thống
        // (bring-up phải tiếp tục báo cáo các phần cứng còn lại).
        const core::Status st = ch_[i].imu->status();
        start_failed_[i]      = !core::is_ok(st);
        if (start_failed_[i]) {
            bringup::notify(*ctx_, "imu", Severity::Error, "IMU%u (%s): port failed: %s", static_cast<unsigned>(i),
                            ch_[i].imu->format_name(), core::to_string(st));
        }
    }
    return core::Status::Ok;
}

void ImuBringup::on_event(const msg::Event& e) {
    svc::ParamChanged pc{};
    if (e.id != svc::evt::kParamChanged || !e.get(pc)) return;
    if (pc.id == params::kImuRateHz) period_ms_ = 1000u / static_cast<std::uint32_t>(pc.value);   // tick kế tiếp áp dụng
    if (pc.id == params::kImuTimeoutMs) timeout_ms_ = static_cast<std::uint32_t>(pc.value);
}

void ImuBringup::on_tick(std::uint64_t now_ms) {
    for (std::size_t i = 0; i < count_; ++i) {
        device::ImuSample s{};
        const bool has            = core::is_ok(ch_[i].samples->read(s));   // Busy (đang ghi dở): bỏ qua lần này
        const device::ImuStats st = ch_[i].imu->stats();
        // last_frame_ms do thread lấy mẫu ghi, có thể mới hơn now_ms -> so sánh dạng cộng (không tràn số)
        const bool fresh        = st.last_frame_ms != 0 && st.last_frame_ms + timeout_ms_ >= now_ms;

        const Link next = fresh ? Link::Alive : (now_ms > start_ms_ + timeout_ms_ ? Link::Lost : Link::Unknown);
        if (next != Link::Unknown && next != link_[i] && !start_failed_[i]) {
            link_[i] = next;
            if (fresh) {
                bringup::notify(*ctx_, "imu", Severity::Info, "IMU%u (%s): data OK", static_cast<unsigned>(i),
                                                       ch_[i].imu->format_name());
            } else {
                bringup::notify(*ctx_, "imu", Severity::Error, "IMU%u (%s): no valid frame", static_cast<unsigned>(i),
                                                       ch_[i].imu->format_name());
            }
        }

        bringup::ImuSample ev{};
        ev.time_ms = static_cast<std::uint32_t>(now_ms);
        ev.frames  = st.frames;
        ev.errors  = st.crc_errors + st.format_errors;
        ev.rate_hz = static_cast<std::uint16_t>(st.rate_hz > 0xFFFF ? 0xFFFF : st.rate_hz);
        ev.imu     = static_cast<std::uint8_t>(i);
        ev.type    = ch_[i].imu->type_id();
        ev.flags   = static_cast<std::uint8_t>((fresh ? bringup::kImuFresh : 0) | (has ? bringup::kImuEverReceived : 0) |
                                             (start_failed_[i] ? bringup::kImuStartFailed : 0));
        ev.temperature = NAN;
        if (has) {
            std::memcpy(ev.gyro, s.gyro, sizeof(ev.gyro));
            std::memcpy(ev.accel, s.accel, sizeof(ev.accel));
            ev.temperature = s.temperature;
            ev.status      = s.status;
            ev.raw_len     = s.raw_len;
            std::memcpy(ev.raw, s.raw, sizeof(ev.raw));
        }
        ctx_->bus.publish(bringup::make_record(bringup::kImuSample, ev));
    }
}

int ImuBringup::on_command(int /*argc*/, char** /*argv*/, svc::CommandOutput& out) {
    const std::uint64_t now = osal::now_ms();
    for (std::size_t i = 0; i < count_; ++i) {
        const device::ImuStats st = ch_[i].imu->stats();
        device::ImuSample s{};
        const bool has = core::is_ok(ch_[i].samples->read(s));
        out.print("IMU%u %-7s %s rate=%lu Hz frames=%lu crc_err=%lu fmt_err=%lu other=%lu age=%ld ms\r\n",
                  static_cast<unsigned>(i), ch_[i].imu->format_name(), start_failed_[i] ? "PORT-FAILED" : "",
                  static_cast<unsigned long>(st.rate_hz), static_cast<unsigned long>(st.frames),
                  static_cast<unsigned long>(st.crc_errors), static_cast<unsigned long>(st.format_errors),
                  static_cast<unsigned long>(st.other_frames),
                  st.last_frame_ms ? (st.last_frame_ms >= now ? 0L : static_cast<long>(now - st.last_frame_ms)) : -1L);
        if (!has) continue;
        out.print("     gyro[rad/s] % .5f % .5f % .5f  accel[m/s2] % .4f % .4f % .4f  status=%04X\r\n     raw:",
                  static_cast<double>(s.gyro[0]), static_cast<double>(s.gyro[1]), static_cast<double>(s.gyro[2]),
                  static_cast<double>(s.accel[0]), static_cast<double>(s.accel[1]), static_cast<double>(s.accel[2]),
                  s.status);
        for (std::size_t k = 0; k < s.raw_len && k < 32; ++k) out.print(" %02X", s.raw[k]);
        out.print("\r\n");
    }
    return 0;
}

}  // namespace app
