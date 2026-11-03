#include "mavlink_dialect.hpp"

#include <cmath>
#include <cstring>

#include "bringup_events.hpp"
#include "bringup_messages.hpp"

namespace product {

namespace bu = app::bringup;
using namespace mavlink;

bool BringupDialect::crc_extra(std::uint32_t msgid, std::uint8_t& out) const {
    // Chỉ bản tin giao diện GỬI xuống mới cần (bản tin chuẩn giao thức đã biết).
    if (msgid == msg::OutputSet::kId) {
        out = msg::OutputSet::kCrcExtra;
        return true;
    }
    return false;
}

void BringupDialect::on_event(const svc::Event& e, Sender& tx) {
    switch (e.id) {
        case bu::kBringupStatus: {
            bu::BringupStatus s{};
            if (!e.get(s)) return;
            msg::BringupStatus m;
            m.time_boot_ms  = s.uptime_ms;
            m.gnss_rx_bytes = s.gnss_rx_bytes;
            m.gnss_tx_bytes = s.gnss_tx_bytes;
            m.gnss_baudrate = s.gnss_baudrate;
            m.gnss_state    = static_cast<std::uint8_t>(s.gnss_state);
            m.gcs_port      = s.gcs_port;
            m.gnss_port     = s.gnss_port;
            m.serial_count  = s.serial_count;
            tx.send(m);
            return;
        }
        case bu::kLoopbackStatus: {
            bu::LoopbackStatus s{};
            if (!e.get(s)) return;
            msg::SerialLoopbackStatus m;
            static_assert(msg::SerialLoopbackStatus::kPorts == bu::kMaxSerial, "port count mismatch");
            std::memcpy(m.pass_count, s.pass, sizeof(m.pass_count));
            std::memcpy(m.fail_count, s.fail, sizeof(m.fail_count));
            std::memcpy(m.role, s.role, sizeof(m.role));
            std::memcpy(m.result, s.result, sizeof(m.result));
            m.ok_mask     = s.ok_mask;
            m.tested_mask = s.tested_mask;
            m.port_count  = s.count;
            tx.send(m);
            return;
        }
        case bu::kIoStatus: {
            bu::IoStatus s{};
            if (!e.get(s)) return;
            msg::IoStatus m;
            m.time_boot_ms = s.time_ms;
            m.input_mask   = s.inputs;
            m.output_mask  = s.outputs;
            m.input_count  = s.input_count;
            m.output_count = s.output_count;
            tx.send(m);
            return;
        }
        case bu::kImuSample: {
            bu::ImuSample s{};
            if (!e.get(s)) return;
            msg::ImuData m;
            m.time_boot_ms = s.time_ms;
            m.frame_count  = s.frames;
            m.error_count  = s.errors;
            std::memcpy(m.gyro, s.gyro, sizeof(m.gyro));
            std::memcpy(m.accel, s.accel, sizeof(m.accel));
            m.temperature = s.temperature;
            m.status      = s.status;
            m.rate_hz     = s.rate_hz;
            m.imu_id      = s.imu;
            m.imu_type    = s.type;
            m.flags       = s.flags;
            m.raw_len     = s.raw_len;
            std::memcpy(m.raw, s.raw, sizeof(m.raw));
            tx.send(m);
            return;
        }
        case bu::kGnssRxData: {   // dữ liệu máy thu -> SERIAL_CONTROL trả lời (chuẩn MAVLink cho "serial passthrough")
            bu::GnssData d{};
            if (!e.get(d)) return;
            SerialControl m;
            m.device = SERIAL_CONTROL_DEV_GPS1;
            m.flags  = SERIAL_CONTROL_FLAG_REPLY;
            m.count  = d.len;
            std::memcpy(m.data, d.data, d.len);
            tx.send(m);
            return;
        }
        case bu::kStatusText: {
            bu::StatusText t{};
            if (!e.get(t)) return;
            StatusText m;
            m.severity = static_cast<std::uint8_t>(t.severity);
            std::memcpy(m.text, t.text, sizeof(m.text));
            tx.send(m);
            return;
        }
        default: return;
    }
}

void BringupDialect::on_message(const Message& m, comm::Services& s, Sender& /*tx*/) {
    if (m.msgid == msg::OutputSet::kId) {
        const auto o = msg::OutputSet::unpack(m.payload);
        s.bus.publish(svc::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{o.mask, o.value}));
        return;
    }
    if (m.msgid == SerialControl::kId) {
        const auto sc = SerialControl::unpack(m.payload);
        if (sc.device != SERIAL_CONTROL_DEV_GPS1 || (sc.flags & SERIAL_CONTROL_FLAG_REPLY)) return;
        bu::GnssTxRequest req{};
        req.baudrate = sc.baudrate;
        req.len      = sc.count;
        std::memcpy(req.data, sc.data, sc.count);
        if (req.len > 0 || req.baudrate != 0) s.bus.publish(svc::Event::make(bu::kGnssTxRequest, req));
    }
}

std::uint8_t BringupDialect::on_command(const CommandLong& c, comm::Services& s, Sender& /*tx*/) {
    if (c.command == MAV_CMD_DO_SET_RELAY) {   // param1 = số kênh output, param2 = 0/1
        const long ch = std::lround(c.param[0]);
        if (ch < 0 || ch > 31) return MAV_RESULT_DENIED;
        const std::uint32_t bit = 1u << ch;
        s.bus.publish(svc::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{bit, c.param[1] >= 0.5f ? bit : 0u}));
        return MAV_RESULT_ACCEPTED;
    }
    return MAV_RESULT_UNSUPPORTED;
}

}  // namespace product
