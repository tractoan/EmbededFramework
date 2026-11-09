#pragma once
// Tập con bản tin chuẩn (common.xml) mà giao thức dùng. Offset theo thứ tự trên dây
// (trường sắp theo kích thước kiểu giảm dần, extension ở cuối). CRC_EXTRA khớp pymavlink/mavgen
// – được kiểm tra chéo bởi tools/mavlink/mavlink_dialect.py.
#include <cstring>

#include "mavlink2/codec.hpp"

namespace mavlink2 {

// Hằng số chuẩn hay dùng
enum : std::uint8_t {
    MAV_TYPE_GENERIC = 0, MAV_TYPE_GCS = 6, MAV_TYPE_ONBOARD_CONTROLLER = 18,
    MAV_AUTOPILOT_INVALID = 8,
    MAV_STATE_BOOT = 1, MAV_STATE_STANDBY = 3, MAV_STATE_ACTIVE = 4, MAV_STATE_CRITICAL = 5,
    MAV_PARAM_TYPE_INT32 = 6,
    MAV_RESULT_ACCEPTED = 0, MAV_RESULT_TEMPORARILY_REJECTED = 1, MAV_RESULT_DENIED = 2,
    MAV_RESULT_UNSUPPORTED = 3, MAV_RESULT_FAILED = 4,
    MAV_SEVERITY_EMERGENCY = 0, MAV_SEVERITY_ALERT = 1, MAV_SEVERITY_CRITICAL = 2, MAV_SEVERITY_ERROR = 3,
    MAV_SEVERITY_WARNING = 4, MAV_SEVERITY_NOTICE = 5, MAV_SEVERITY_INFO = 6, MAV_SEVERITY_DEBUG = 7,
    SERIAL_CONTROL_DEV_GPS1 = 1, SERIAL_CONTROL_DEV_GPS2 = 2,
    SERIAL_CONTROL_FLAG_REPLY = 1, SERIAL_CONTROL_FLAG_RESPOND = 2, SERIAL_CONTROL_FLAG_EXCLUSIVE = 4,
    SERIAL_CONTROL_FLAG_BLOCKING = 8, SERIAL_CONTROL_FLAG_MULTI = 16,
};
enum : std::uint16_t {
    MAV_CMD_DO_SET_RELAY = 181,
    MAV_CMD_PREFLIGHT_STORAGE = 245,
    MAV_CMD_REQUEST_MESSAGE = 512,
};

struct Heartbeat {
    static constexpr std::uint32_t kId = 0;
    static constexpr std::uint8_t kCrcExtra = 50;
    static constexpr std::size_t kLen = 9;
    std::uint32_t custom_mode = 0;
    std::uint8_t type = MAV_TYPE_GENERIC, autopilot = MAV_AUTOPILOT_INVALID, base_mode = 0,
                 system_status = MAV_STATE_ACTIVE, mavlink_version = 3;
    void pack(std::uint8_t* p) const {
        wire::put_u32(p, 0, custom_mode);
        p[4] = type; p[5] = autopilot; p[6] = base_mode; p[7] = system_status; p[8] = mavlink_version;
    }
    static Heartbeat unpack(const std::uint8_t* p) {
        Heartbeat m;
        m.custom_mode = wire::get_u32(p, 0);
        m.type = p[4]; m.autopilot = p[5]; m.base_mode = p[6]; m.system_status = p[7]; m.mavlink_version = p[8];
        return m;
    }
};

// GPS_FIX_TYPE
enum : std::uint8_t {
    GPS_FIX_TYPE_NO_GPS = 0, GPS_FIX_TYPE_NO_FIX = 1, GPS_FIX_TYPE_2D_FIX = 2, GPS_FIX_TYPE_3D_FIX = 3,
    GPS_FIX_TYPE_DGPS = 4, GPS_FIX_TYPE_RTK_FLOAT = 5, GPS_FIX_TYPE_RTK_FIXED = 6,
};

struct GpsRawInt {
    static constexpr std::uint32_t kId = 24;
    static constexpr std::uint8_t kCrcExtra = 24;
    static constexpr std::size_t kLen = 52;   // gồm extension
    std::uint64_t time_usec = 0;              // UNIX epoch hoặc thời gian từ khởi động
    std::int32_t lat = 0, lon = 0, alt = 0;   // degE7, degE7, mm (MSL)
    std::uint16_t eph = 0xFFFF, epv = 0xFFFF, vel = 0xFFFF, cog = 0xFFFF;   // HDOP x100, VDOP x100, cm/s, cdeg
    std::uint8_t fix_type = GPS_FIX_TYPE_NO_GPS, satellites_visible = 0xFF;
    std::int32_t alt_ellipsoid = 0;
    std::uint32_t h_acc = 0, v_acc = 0, vel_acc = 0, hdg_acc = 0;
    std::uint16_t yaw = 0;
    void pack(std::uint8_t* p) const {
        wire::put_u32(p, 0, static_cast<std::uint32_t>(time_usec));
        wire::put_u32(p, 4, static_cast<std::uint32_t>(time_usec >> 32));
        wire::put_i32(p, 8, lat);
        wire::put_i32(p, 12, lon);
        wire::put_i32(p, 16, alt);
        wire::put_u16(p, 20, eph);
        wire::put_u16(p, 22, epv);
        wire::put_u16(p, 24, vel);
        wire::put_u16(p, 26, cog);
        p[28] = fix_type; p[29] = satellites_visible;
        wire::put_i32(p, 30, alt_ellipsoid);
        wire::put_u32(p, 34, h_acc);
        wire::put_u32(p, 38, v_acc);
        wire::put_u32(p, 42, vel_acc);
        wire::put_u32(p, 46, hdg_acc);
        wire::put_u16(p, 50, yaw);
    }
};

struct ParamRequestRead {
    static constexpr std::uint32_t kId = 20;
    static constexpr std::uint8_t kCrcExtra = 214;
    static constexpr std::size_t kLen = 20;
    std::int16_t param_index = -1;
    std::uint8_t target_system = 0, target_component = 0;
    char param_id[17]{};   // +1 cho ký tự kết thúc
    static ParamRequestRead unpack(const std::uint8_t* p) {
        ParamRequestRead m;
        m.param_index = static_cast<std::int16_t>(wire::get_u16(p, 0));
        m.target_system = p[2]; m.target_component = p[3];
        std::memcpy(m.param_id, p + 4, 16);
        return m;
    }
};

struct ParamRequestList {
    static constexpr std::uint32_t kId = 21;
    static constexpr std::uint8_t kCrcExtra = 159;
    static constexpr std::size_t kLen = 2;
    std::uint8_t target_system = 0, target_component = 0;
    static ParamRequestList unpack(const std::uint8_t* p) { return ParamRequestList{p[0], p[1]}; }
};

struct ParamValue {
    static constexpr std::uint32_t kId = 22;
    static constexpr std::uint8_t kCrcExtra = 220;
    static constexpr std::size_t kLen = 25;
    float param_value = 0;
    std::uint16_t param_count = 0, param_index = 0;
    char param_id[16]{};
    std::uint8_t param_type = MAV_PARAM_TYPE_INT32;
    void pack(std::uint8_t* p) const {
        wire::put_f32(p, 0, param_value);
        wire::put_u16(p, 4, param_count);
        wire::put_u16(p, 6, param_index);
        std::memcpy(p + 8, param_id, 16);
        p[24] = param_type;
    }
};

struct ParamSet {
    static constexpr std::uint32_t kId = 23;
    static constexpr std::uint8_t kCrcExtra = 168;
    static constexpr std::size_t kLen = 23;
    float param_value = 0;
    std::uint8_t target_system = 0, target_component = 0;
    char param_id[17]{};
    std::uint8_t param_type = 0;
    static ParamSet unpack(const std::uint8_t* p) {
        ParamSet m;
        m.param_value = wire::get_f32(p, 0);
        m.target_system = p[4]; m.target_component = p[5];
        std::memcpy(m.param_id, p + 6, 16);
        m.param_type = p[22];
        return m;
    }
};

struct CommandLong {
    static constexpr std::uint32_t kId = 76;
    static constexpr std::uint8_t kCrcExtra = 152;
    static constexpr std::size_t kLen = 33;
    float param[7]{};
    std::uint16_t command = 0;
    std::uint8_t target_system = 0, target_component = 0, confirmation = 0;
    void pack(std::uint8_t* p) const {
        for (int i = 0; i < 7; ++i) wire::put_f32(p, 4 * i, param[i]);
        wire::put_u16(p, 28, command);
        p[30] = target_system; p[31] = target_component; p[32] = confirmation;
    }
    static CommandLong unpack(const std::uint8_t* p) {
        CommandLong m;
        for (int i = 0; i < 7; ++i) m.param[i] = wire::get_f32(p, 4 * i);
        m.command = wire::get_u16(p, 28);
        m.target_system = p[30]; m.target_component = p[31]; m.confirmation = p[32];
        return m;
    }
};

struct CommandAck {
    static constexpr std::uint32_t kId = 77;
    static constexpr std::uint8_t kCrcExtra = 143;
    static constexpr std::size_t kLen = 10;   // gồm extension
    std::uint16_t command = 0;
    std::uint8_t result = 0, progress = 0;
    std::int32_t result_param2 = 0;
    std::uint8_t target_system = 0, target_component = 0;
    void pack(std::uint8_t* p) const {
        wire::put_u16(p, 0, command);
        p[2] = result; p[3] = progress;
        wire::put_i32(p, 4, result_param2);
        p[8] = target_system; p[9] = target_component;
    }
};

struct SerialControl {
    static constexpr std::uint32_t kId = 126;
    static constexpr std::uint8_t kCrcExtra = 220;
    static constexpr std::size_t kLen = 81;   // gồm extension target_system/component
    static constexpr std::size_t kMaxData = 70;
    std::uint32_t baudrate = 0;
    std::uint16_t timeout = 0;
    std::uint8_t device = 0, flags = 0, count = 0;
    std::uint8_t data[kMaxData]{};
    std::uint8_t target_system = 0, target_component = 0;
    void pack(std::uint8_t* p) const {
        wire::put_u32(p, 0, baudrate);
        wire::put_u16(p, 4, timeout);
        p[6] = device; p[7] = flags; p[8] = count;
        std::memcpy(p + 9, data, kMaxData);
        p[79] = target_system; p[80] = target_component;
    }
    static SerialControl unpack(const std::uint8_t* p) {
        SerialControl m;
        m.baudrate = wire::get_u32(p, 0);
        m.timeout = wire::get_u16(p, 4);
        m.device = p[6]; m.flags = p[7]; m.count = p[8] > kMaxData ? kMaxData : p[8];
        std::memcpy(m.data, p + 9, kMaxData);
        m.target_system = p[79]; m.target_component = p[80];
        return m;
    }
};

struct StatusText {
    static constexpr std::uint32_t kId = 253;
    static constexpr std::uint8_t kCrcExtra = 83;
    static constexpr std::size_t kLen = 54;   // gồm extension id/chunk_seq
    std::uint8_t severity = MAV_SEVERITY_INFO;
    char text[50]{};   // không bắt buộc kết thúc '\0' khi đủ 50 ký tự
    std::uint16_t id = 0;
    std::uint8_t chunk_seq = 0;
    void pack(std::uint8_t* p) const {
        p[0] = severity;
        std::memcpy(p + 1, text, 50);
        wire::put_u16(p, 51, id);
        p[53] = chunk_seq;
    }
    static StatusText unpack(const std::uint8_t* p) {
        StatusText m;
        m.severity = p[0];
        std::memcpy(m.text, p + 1, 50);
        m.id = wire::get_u16(p, 51);
        m.chunk_seq = p[53];
        return m;
    }
};

}  // namespace mavlink2
