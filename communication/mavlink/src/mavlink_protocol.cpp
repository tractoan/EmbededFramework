#include "mavlink/mavlink_protocol.hpp"

#include <cmath>
#include <cstdio>

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace mavlink {

namespace {
constexpr const char* kTag = "mavlink";
}

// ------------------------------------------------------------- vòng đời
core::Status MavlinkV1Protocol::attach(comm::IByteStream& io, comm::Services& services) {
    services_ = &services;
    parser_.reset();
    param_cursor_      = -1;
    last_heartbeat_ms_ = 0;
    last_gcs_ms_       = 0;
    gcs_connected_     = false;
    {
        osal::LockGuard g(tx_mtx_);
        io_ = &io;
    }
    return services.bus.subscribe(msg::evt::kAny, this);
}

void MavlinkV1Protocol::detach() {
    if (services_ != nullptr) services_->bus.unsubscribe(msg::evt::kAny, this);
    osal::LockGuard g(tx_mtx_);   // on_event đang chạy dở ở thread khác sẽ thấy io_ == nullptr
    io_ = nullptr;
}

// ------------------------------------------------------------------- gửi
void MavlinkV1Protocol::send(std::uint8_t msgid, const std::uint8_t* payload, std::size_t len) {
    std::uint8_t frame[kMaxFrame];
    osal::LockGuard g(tx_mtx_);
    if (io_ == nullptr) return;
    const std::size_t n = encode(frame, seq_, map_.system_id, map_.component_id, msgid, payload, len);
    if (n == 0) return;
    ++seq_;
    io_->write(frame, n, 100);
}

void MavlinkV1Protocol::send_heartbeat() {
    std::uint8_t p[9];
    put_u32(p, 0);                       // custom_mode
    p[4] = map_.mav_type;                // type
    p[5] = mav::kAutopilotInvalid;       // autopilot: không phải bộ lái
    p[6] = 0;                            // base_mode
    p[7] = mav_state_.load();            // system_status
    p[8] = mav::kMavlinkVersion;
    send(msgid::kHeartbeat, p, sizeof(p));
}

void MavlinkV1Protocol::send_param(std::size_t index) {
    std::int32_t value         = 0;
    const svc::ParamDef* def   = services_->params.at(index, &value);
    if (def == nullptr) return;
    std::uint8_t p[25];
    put_f32(p, static_cast<float>(value));                                   // param_value
    put_u16(p + 4, static_cast<std::uint16_t>(services_->params.count()));   // param_count
    put_u16(p + 6, static_cast<std::uint16_t>(index));                       // param_index
    put_str(p + 8, 16, def->name);                                           // param_id
    p[24] = mav::kParamTypeReal32;
    send(msgid::kParamValue, p, sizeof(p));
}

void MavlinkV1Protocol::send_ack(std::uint16_t command, std::uint8_t result) {
    std::uint8_t p[3];
    put_u16(p, command);
    p[2] = result;
    send(msgid::kCommandAck, p, sizeof(p));
}

// ------------------------------------------------------------------- nhận
void MavlinkV1Protocol::on_rx(const std::uint8_t* data, std::size_t len) {
    now_ms_ = osal::now_ms();
    parser_.feed(data, len, &MavlinkV1Protocol::on_frame, this);
}

void MavlinkV1Protocol::on_frame(void* self, const Frame& f) { static_cast<MavlinkV1Protocol*>(self)->handle(f); }

bool MavlinkV1Protocol::for_me(std::uint8_t target_system, std::uint8_t target_component) const {
    return (target_system == map_.system_id || target_system == 0) &&
           (target_component == map_.component_id || target_component == 0);
}

int MavlinkV1Protocol::find_param(const std::uint8_t* id16) const {
    for (std::size_t i = 0; i < services_->params.count(); ++i) {
        const svc::ParamDef* d = services_->params.at(i);
        if (d != nullptr && str_equals(id16, 16, d->name)) return static_cast<int>(i);
    }
    return -1;
}

void MavlinkV1Protocol::handle(const Frame& f) {
    const std::uint8_t* p = f.payload;
    switch (f.msgid) {
        case msgid::kHeartbeat:
            if (f.sysid != map_.system_id) {   // nhịp tim của trạm mặt đất / thiết bị khác
                last_gcs_ms_ = now_ms_;
                if (!gcs_connected_.exchange(true)) LOGI(kTag, "GCS sysid %u connected", static_cast<unsigned>(f.sysid));
            }
            break;
        case msgid::kParamRequestList:
            if (for_me(p[0], p[1])) param_cursor_ = 0;   // gửi dần trong poll()
            break;
        case msgid::kParamRequestRead: {
            if (!for_me(p[2], p[3])) break;
            const auto index = static_cast<std::int16_t>(get_u16(p));
            const int i      = index >= 0 ? index : find_param(p + 4);
            if (i >= 0 && static_cast<std::size_t>(i) < services_->params.count()) send_param(static_cast<std::size_t>(i));
            break;
        }
        case msgid::kParamSet: {
            if (!for_me(p[4], p[5])) break;
            const int i = find_param(p + 6);
            if (i < 0) break;   // không có tham số này: không trả lời (theo param protocol)
            const svc::ParamDef* d = services_->params.at(static_cast<std::size_t>(i));
            const float v          = get_f32(p);
            if (std::isfinite(v) && v >= -2147483520.0f && v <= 2147483520.0f) {
                (void)services_->params.set(d->id, static_cast<std::int32_t>(std::lround(v)));
            }
            send_param(static_cast<std::size_t>(i));   // luôn trả lại giá trị HIỆN TẠI (kể cả khi bị từ chối)
            break;
        }
        case msgid::kCommandLong: {
            if (!for_me(p[30], p[31])) break;
            const std::uint16_t cmd = get_u16(p + 28);
            float params[7];
            for (int k = 0; k < 7; ++k) params[k] = get_f32(p + 4 * k);
            std::uint8_t result = mav::kResultUnsupported;
            for (std::size_t k = 0; k < map_.command_count; ++k) {
                if (map_.commands[k].command == cmd) {
                    result = map_.commands[k].handle(*services_, params);
                    break;
                }
            }
            send_ack(cmd, result);
            break;
        }
        default: break;   // các message khác: bỏ qua
    }
}

void MavlinkV1Protocol::poll(std::uint64_t now_ms) {
    now_ms_ = now_ms;
    if (last_heartbeat_ms_ == 0 || now_ms - last_heartbeat_ms_ >= map_.heartbeat_ms) {
        last_heartbeat_ms_ = now_ms;
        send_heartbeat();
    }
    for (std::size_t k = 0; param_cursor_ >= 0 && k < kParamsPerPoll; ++k) {
        if (static_cast<std::size_t>(param_cursor_) >= services_->params.count()) {
            param_cursor_ = -1;
            break;
        }
        send_param(static_cast<std::size_t>(param_cursor_++));
    }
    if (gcs_connected_.load() && now_ms - last_gcs_ms_ > kGcsTimeoutMs) {
        gcs_connected_ = false;
        LOGW(kTag, "GCS heartbeat lost");
    }
}

// ----------------------------------------------- Event -> MAVLink (mọi thread)
void MavlinkV1Protocol::on_event(const msg::Event& e) {
    for (std::size_t k = 0; k < map_.state_count; ++k) {
        std::uint8_t s = 0;
        if (map_.states[k].event == e.id && map_.states[k].state(e, s)) mav_state_ = s;
    }
    for (std::size_t k = 0; k < map_.telemetry_count; ++k) {
        const Telemetry& t = map_.telemetry[k];
        float v            = 0;
        if (t.event != e.id || !t.extract(e, v)) continue;
        std::uint8_t p[18];
        put_u32(p, static_cast<std::uint32_t>(osal::now_ms()));   // time_boot_ms
        put_f32(p + 4, v);
        put_str(p + 8, 10, t.name);
        send(msgid::kNamedValueFloat, p, sizeof(p));
    }
    for (std::size_t k = 0; k < map_.status_text_count; ++k) {
        const StatusText& st = map_.status_texts[k];
        if (st.event != e.id) continue;
        char text[51]         = {};
        std::uint8_t severity = mav::kSeverityInfo;
        if (!st.format(e, severity, text, sizeof(text))) continue;
        std::uint8_t p[51];
        p[0] = severity;
        put_str(p + 1, 50, text);
        send(msgid::kStatustext, p, sizeof(p));
    }
}

}  // namespace mavlink
