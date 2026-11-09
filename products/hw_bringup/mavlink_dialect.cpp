#include "mavlink_dialect.hpp"

#include <cmath>
#include <cstring>

#include "bringup_events.hpp"
#include "bringup_messages.hpp"

namespace product {

namespace bu = app::bringup;
using namespace mavlink2;

bool BringupDialect::crc_extra(std::uint32_t msgid, std::uint8_t& out) const {
    // Chỉ bản tin giao diện GỬI xuống mới cần (bản tin chuẩn giao thức đã biết).
    if (msgid == mavmsg::OutputSet::kId) {
        out = mavmsg::OutputSet::kCrcExtra;
        return true;
    }
    return false;
}

void BringupDialect::on_event(const msg::Event& e, Sender& tx) {
    switch (e.id) {
        case bu::kBringupStatus: {
            bu::BringupStatus s{};
            if (!bu::get_record(e, s)) return;
            mavmsg::BringupStatus m;
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
            bu::LoopbackHeader h{};
            if (!e.get(h) || e.buffer.size() != h.count * sizeof(bu::PortResult)) return;
            mavmsg::SerialLoopbackStatus m;
            const std::size_t n = h.count < mavmsg::SerialLoopbackStatus::kPorts ? h.count : mavmsg::SerialLoopbackStatus::kPorts;
            for (std::size_t i = 0; i < n; ++i) {   // bản tin chứa tối đa 12 cổng
                bu::PortResult r{};
                std::memcpy(&r, e.buffer.data() + i * sizeof(r), sizeof(r));
                m.pass_count[i] = r.pass;
                m.fail_count[i] = r.fail;
                m.role[i]       = r.role;
                m.result[i]     = r.result;
            }
            const auto mask = static_cast<std::uint16_t>((1u << n) - 1u);
            m.ok_mask     = static_cast<std::uint16_t>(h.ok_mask & mask);
            m.tested_mask = static_cast<std::uint16_t>(h.tested_mask & mask);
            m.port_count  = static_cast<std::uint8_t>(n);
            tx.send(m);
            return;
        }
        case bu::kIoStatus: {
            bu::IoStatus s{};
            if (!e.get(s)) return;
            mavmsg::IoStatus m;
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
            if (!bu::get_record(e, s)) return;
            mavmsg::ImuData m;
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
            const std::uint8_t* p = e.buffer.data();
            for (std::size_t off = 0, n = e.buffer.size(); off < n; off += SerialControl::kMaxData) {
                SerialControl m;   // mỗi bản tin chở tối đa 70 byte
                m.device = SERIAL_CONTROL_DEV_GPS1;
                m.flags  = SERIAL_CONTROL_FLAG_REPLY;
                m.count  = static_cast<std::uint8_t>(n - off < SerialControl::kMaxData ? n - off : SerialControl::kMaxData);
                std::memcpy(m.data, p + off, m.count);
                tx.send(m);
            }
            return;
        }
        case bu::kStatusText: {
            bu::StatusHeader h{};
            if (!e.get(h)) return;
            StatusText m;
            m.severity = static_cast<std::uint8_t>(h.severity);
            std::memcpy(m.text, e.buffer.data(), e.buffer.size() < sizeof(m.text) ? e.buffer.size() : sizeof(m.text));
            tx.send(m);
            return;
        }
        default: return;
    }
}

void BringupDialect::on_message(const Message& m, comm::Services& s, Sender& /*tx*/) {
    if (m.msgid == mavmsg::OutputSet::kId) {
        const auto o = mavmsg::OutputSet::unpack(m.payload);
        s.bus.publish(msg::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{o.mask, o.value}));
        return;
    }
    if (m.msgid == SerialControl::kId) {
        const auto sc = SerialControl::unpack(m.payload);
        if (sc.device != SERIAL_CONTROL_DEV_GPS1 || (sc.flags & SERIAL_CONTROL_FLAG_REPLY)) return;
        if (sc.count == 0 && sc.baudrate == 0) return;
        msg::Buffer data;
        if (sc.count > 0) {
            data = msg::Buffer::copy_of(sc.data, sc.count);
            if (!data) return;   // hết pool buffer: bỏ lệnh (GUI không nhận được phản hồi -> gửi lại)
        }
        s.bus.publish(msg::Event::make(bu::kGnssTxRequest, bu::GnssTxHeader{sc.baudrate}, std::move(data)));
    }
}

std::uint8_t BringupDialect::on_command(const CommandLong& c, comm::Services& s, Sender& /*tx*/) {
    if (c.command == MAV_CMD_DO_SET_RELAY) {   // param1 = số kênh output, param2 = 0/1
        const long ch = std::lround(c.param[0]);
        if (ch < 0 || ch > 31) return MAV_RESULT_DENIED;
        const std::uint32_t bit = 1u << ch;
        s.bus.publish(msg::Event::make(bu::kOutputSetRequest, bu::OutputSetRequest{bit, c.param[1] >= 0.5f ? bit : 0u}));
        return MAV_RESULT_ACCEPTED;
    }
    return MAV_RESULT_UNSUPPORTED;
}

}  // namespace product
