#include "mavlink/mavlink_protocol.hpp"

#include <cmath>
#include <cstring>

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace mavlink {

namespace {
constexpr const char* kTag = "mavlink";
}

bool MavlinkProtocol::standard_crc_extra(std::uint32_t msgid, std::uint8_t& out) {
    switch (msgid) {
        case Heartbeat::kId:        out = Heartbeat::kCrcExtra; return true;
        case ParamRequestRead::kId: out = ParamRequestRead::kCrcExtra; return true;
        case ParamRequestList::kId: out = ParamRequestList::kCrcExtra; return true;
        case ParamSet::kId:         out = ParamSet::kCrcExtra; return true;
        case ParamValue::kId:       out = ParamValue::kCrcExtra; return true;
        case CommandLong::kId:      out = CommandLong::kCrcExtra; return true;
        case CommandAck::kId:       out = CommandAck::kCrcExtra; return true;
        case SerialControl::kId:    out = SerialControl::kCrcExtra; return true;
        case StatusText::kId:       out = StatusText::kCrcExtra; return true;
        default:                    return false;
    }
}

bool MavlinkProtocol::lookup(std::uint32_t msgid, std::uint8_t& crc_extra, const void* self) {
    if (standard_crc_extra(msgid, crc_extra)) return true;
    return static_cast<const MavlinkProtocol*>(self)->dialect_.crc_extra(msgid, crc_extra);
}

core::Status MavlinkProtocol::attach(comm::IByteStream& io, comm::Services& services) {
    services_     = &services;
    parser_.reset();
    param_stream_ = -1;
    last_hb_tx_   = 0;
    gcs_connected_ = false;
    io_ = &io;
    return services.bus.subscribe(svc::evt::kAny, this);
}

void MavlinkProtocol::detach() {
    if (services_ != nullptr) services_->bus.unsubscribe(svc::evt::kAny, this);
    io_ = nullptr;
    services_ = nullptr;
    gcs_connected_ = false;
}

bool MavlinkProtocol::probe(const std::uint8_t* data, std::size_t len) const {
    return starts_with_valid_frame(data, len, &lookup, this);
}

bool MavlinkProtocol::send_raw(std::uint32_t msgid, std::uint8_t crc_extra, const std::uint8_t* payload, std::size_t len) {
    comm::IByteStream* io = io_.load();
    if (io == nullptr) return false;
    std::uint8_t frame[kMaxFrame];
    const std::size_t n = encode(frame, sizeof(frame), seq_.fetch_add(1), cfg_.sysid, cfg_.compid, msgid, crc_extra, payload, len);
    if (n == 0) return false;
    const bool ok = io->write(frame, n, 100) == n;
    if (ok) tx_msgs_.fetch_add(1);
    return ok;
}

void MavlinkProtocol::on_event(const svc::Event& e) {
    if (io_.load() != nullptr) dialect_.on_event(e, *this);
}

void MavlinkProtocol::on_rx(const std::uint8_t* data, std::size_t len) {
    parser_.feed(data, len, [this](const Message& m) { handle(m); });
}

void MavlinkProtocol::poll(std::uint64_t now_ms) {
    if (last_hb_tx_ == 0 || now_ms - last_hb_tx_ >= cfg_.heartbeat_ms) {
        last_hb_tx_ = now_ms;
        Heartbeat hb;
        hb.type          = cfg_.mav_type;
        hb.autopilot     = MAV_AUTOPILOT_INVALID;
        hb.system_status = MAV_STATE_ACTIVE;
        send(hb);
    }
    // now_ms có thể được lấy TRƯỚC heartbeat vừa nhận -> so sánh dạng cộng để không tràn số không dấu
    if (gcs_connected_ && now_ms > last_gcs_hb_ + cfg_.gcs_timeout_ms) {
        gcs_connected_ = false;
        LOGW(kTag, "GCS heartbeat lost");
    }
    if (param_stream_ >= 0 && services_ != nullptr) {
        for (std::uint8_t k = 0; k < cfg_.params_per_poll && param_stream_ >= 0; ++k) {
            if (static_cast<std::size_t>(param_stream_) >= services_->params.count()) {
                param_stream_ = -1;
                break;
            }
            send_param(static_cast<std::size_t>(param_stream_++));
        }
    }
}

void MavlinkProtocol::send_param(std::size_t index) {
    std::int32_t value = 0;
    const svc::ParamDef* def = services_->params.at(index, &value);
    if (def == nullptr) return;
    ParamValue pv;
    pv.param_value = static_cast<float>(value);   // mã hóa kiểu "C-cast" (chính xác tới 2^24)
    pv.param_count = static_cast<std::uint16_t>(services_->params.count());
    pv.param_index = static_cast<std::uint16_t>(index);
    std::strncpy(pv.param_id, def->name, sizeof(pv.param_id));   // tên > 16 ký tự bị cắt
    send(pv);
}

void MavlinkProtocol::handle(const Message& m) {
    rx_msgs_.fetch_add(1);
    const std::uint8_t* p = m.payload;
    switch (m.msgid) {
        case Heartbeat::kId: {
            if (m.sysid == cfg_.sysid && m.compid == cfg_.compid) return;   // tiếng vọng của chính mình
            last_gcs_hb_ = osal::now_ms();
            if (!gcs_connected_) {
                gcs_connected_ = true;
                LOGI(kTag, "GCS connected (sys %u comp %u)", m.sysid, m.compid);
            }
            return;
        }
        case ParamRequestList::kId: {
            const auto r = ParamRequestList::unpack(p);
            if (for_me(r.target_system, r.target_component)) param_stream_ = 0;
            return;
        }
        case ParamRequestRead::kId: {
            const auto r = ParamRequestRead::unpack(p);
            if (!for_me(r.target_system, r.target_component)) return;
            if (r.param_index >= 0) return send_param(static_cast<std::size_t>(r.param_index));
            for (std::size_t i = 0; i < services_->params.count(); ++i) {
                const svc::ParamDef* d = services_->params.at(i);
                if (d != nullptr && std::strncmp(d->name, r.param_id, 16) == 0) return send_param(i);
            }
            return;
        }
        case ParamSet::kId: {
            const auto r = ParamSet::unpack(p);
            if (!for_me(r.target_system, r.target_component)) return;
            for (std::size_t i = 0; i < services_->params.count(); ++i) {
                const svc::ParamDef* d = services_->params.at(i);
                if (d == nullptr || std::strncmp(d->name, r.param_id, 16) != 0) continue;
                const core::Status st = services_->params.set(d->id, static_cast<std::int32_t>(std::lround(r.param_value)));
                if (!core::is_ok(st)) LOGW(kTag, "PARAM_SET %s rejected: %s", d->name, core::to_string(st));
                return send_param(i);   // luôn trả giá trị hiện tại (chuẩn: xác nhận bằng PARAM_VALUE)
            }
            return;
        }
        case CommandLong::kId: {
            const auto c = CommandLong::unpack(p);
            if (for_me(c.target_system, c.target_component)) handle_command(c, m.sysid, m.compid);
            return;
        }
        default: dialect_.on_message(m, *services_, *this); return;
    }
}

void MavlinkProtocol::handle_command(const CommandLong& c, std::uint8_t from_sys, std::uint8_t from_comp) {
    std::uint8_t result = MAV_RESULT_UNSUPPORTED;
    if (c.command == MAV_CMD_PREFLIGHT_STORAGE) {
        const int action = static_cast<int>(std::lround(c.param[0]));   // 0 = đọc từ NV, 1 = ghi NV, 2 = mặc định
        core::Status st  = core::Status::InvalidArg;
        if (action == 0) st = services_->params.load();
        if (action == 1) st = services_->params.save();
        if (action == 2) st = services_->params.reset_defaults();
        result = core::is_ok(st) ? MAV_RESULT_ACCEPTED : MAV_RESULT_FAILED;
    } else {
        result = dialect_.on_command(c, *services_, *this);
    }
    CommandAck ack;
    ack.command          = c.command;
    ack.result           = result;
    ack.target_system    = from_sys;
    ack.target_component = from_comp;
    send(ack);
}

}  // namespace mavlink
