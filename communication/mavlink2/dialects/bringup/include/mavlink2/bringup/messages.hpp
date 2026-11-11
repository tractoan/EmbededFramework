#pragma once
// Dialect MAVLink "bringup" (định nghĩa gốc: tools/mavlink/bringup.xml): bản tin 62000..62005.
// Chỉ là ĐỊNH NGHĨA TRÊN DÂY (struct, offset, CRC_EXTRA, pack/unpack) – không phụ thuộc nghiệp vụ, nên dùng
// lại được cho mọi sản phẩm/công cụ nói dialect này. Ánh xạ Event <-> bản tin là việc của sản phẩm
// (vd. products/hw_bringup/mavlink_dialect.cpp).
// Offset theo thứ tự trên dây; CRC_EXTRA khớp mavgen – kiểm tra chéo: python3 tools/mavlink/mavlink_dialect.py --check
#include <cstring>

#include "mavlink2/codec.hpp"

namespace mavlink2::bringup {

using namespace ::mavlink2::wire;

struct BringupStatus {
    static constexpr std::uint32_t kId = 62000;
    static constexpr std::uint8_t kCrcExtra = 153;
    static constexpr std::size_t kLen = 20;
    std::uint32_t time_boot_ms = 0, gnss_rx_bytes = 0, gnss_tx_bytes = 0, gnss_baudrate = 0;
    std::uint8_t gnss_state = 0, gcs_port = 0xFF, gnss_port = 0xFF, serial_count = 0;
    void pack(std::uint8_t* p) const {
        put_u32(p, 0, time_boot_ms);
        put_u32(p, 4, gnss_rx_bytes);
        put_u32(p, 8, gnss_tx_bytes);
        put_u32(p, 12, gnss_baudrate);
        p[16] = gnss_state; p[17] = gcs_port; p[18] = gnss_port; p[19] = serial_count;
    }
};

struct SerialLoopbackStatus {
    static constexpr std::uint32_t kId = 62001;
    static constexpr std::uint8_t kCrcExtra = 131;
    static constexpr std::size_t kLen = 125;
    static constexpr std::size_t kPorts = 12;
    std::uint32_t pass_count[kPorts]{}, fail_count[kPorts]{};
    std::uint16_t ok_mask = 0, tested_mask = 0;
    std::uint8_t port_count = 0;
    std::uint8_t role[kPorts]{}, result[kPorts]{};
    void pack(std::uint8_t* p) const {
        for (std::size_t i = 0; i < kPorts; ++i) {
            put_u32(p, 4 * i, pass_count[i]);
            put_u32(p, 48 + 4 * i, fail_count[i]);
        }
        put_u16(p, 96, ok_mask);
        put_u16(p, 98, tested_mask);
        p[100] = port_count;
        std::memcpy(p + 101, role, kPorts);
        std::memcpy(p + 113, result, kPorts);
    }
};

struct IoStatus {
    static constexpr std::uint32_t kId = 62002;
    static constexpr std::uint8_t kCrcExtra = 0;
    static constexpr std::size_t kLen = 14;
    std::uint32_t time_boot_ms = 0, input_mask = 0, output_mask = 0;
    std::uint8_t input_count = 0, output_count = 0;
    void pack(std::uint8_t* p) const {
        put_u32(p, 0, time_boot_ms);
        put_u32(p, 4, input_mask);
        put_u32(p, 8, output_mask);
        p[12] = input_count; p[13] = output_count;
    }
};

struct OutputSet {
    static constexpr std::uint32_t kId = 62003;
    static constexpr std::uint8_t kCrcExtra = 2;
    static constexpr std::size_t kLen = 10;
    std::uint32_t mask = 0, value = 0;
    std::uint8_t target_system = 0, target_component = 0;
    void pack(std::uint8_t* p) const {
        put_u32(p, 0, mask);
        put_u32(p, 4, value);
        p[8] = target_system; p[9] = target_component;
    }
    static OutputSet unpack(const std::uint8_t* p) {
        OutputSet m;
        m.mask = get_u32(p, 0);
        m.value = get_u32(p, 4);
        m.target_system = p[8]; m.target_component = p[9];
        return m;
    }
};

struct ImuData {
    static constexpr std::uint32_t kId = 62004;
    static constexpr std::uint8_t kCrcExtra = 79;
    static constexpr std::size_t kLen = 96;
    std::uint32_t time_boot_ms = 0, frame_count = 0, error_count = 0;
    float gyro[3]{}, accel[3]{};
    float temperature = 0;
    std::uint16_t status = 0, rate_hz = 0;
    std::uint8_t imu_id = 0, imu_type = 0, flags = 0, raw_len = 0;
    std::uint8_t raw[48]{};
    void pack(std::uint8_t* p) const {
        put_u32(p, 0, time_boot_ms);
        put_u32(p, 4, frame_count);
        put_u32(p, 8, error_count);
        for (int i = 0; i < 3; ++i) {
            put_f32(p, 12 + 4 * i, gyro[i]);
            put_f32(p, 24 + 4 * i, accel[i]);
        }
        put_f32(p, 36, temperature);
        put_u16(p, 40, status);
        put_u16(p, 42, rate_hz);
        p[44] = imu_id; p[45] = imu_type; p[46] = flags; p[47] = raw_len;
        std::memcpy(p + 48, raw, 48);
    }
};

struct GnssFix {
    static constexpr std::uint32_t kId = 62005;
    static constexpr std::uint8_t kCrcExtra = 219;
    static constexpr std::size_t kLen = 37;
    std::uint64_t time_unix_usec = 0;
    std::uint32_t time_utc_ms = 0;
    std::int32_t lat = 0, lon = 0, alt = 0;
    std::uint16_t year = 0, hdop = 0xFFFF, speed = 0xFFFF, course = 0xFFFF;
    std::uint8_t month = 0, day = 0, fix_quality = 0, satellites = 0, flags = 0;
    void pack(std::uint8_t* p) const {
        put_u32(p, 0, static_cast<std::uint32_t>(time_unix_usec));
        put_u32(p, 4, static_cast<std::uint32_t>(time_unix_usec >> 32));
        put_u32(p, 8, time_utc_ms);
        put_i32(p, 12, lat);
        put_i32(p, 16, lon);
        put_i32(p, 20, alt);
        put_u16(p, 24, year);
        put_u16(p, 26, hdop);
        put_u16(p, 28, speed);
        put_u16(p, 30, course);
        p[32] = month; p[33] = day; p[34] = fix_quality; p[35] = satellites; p[36] = flags;
    }
};

// CRC_EXTRA của các bản tin dialect mà THIẾT BỊ nhận (phía giao diện gửi xuống) – cho Parser.
inline bool rx_crc_extra(std::uint32_t msgid, std::uint8_t& out) {
    if (msgid == OutputSet::kId) {
        out = OutputSet::kCrcExtra;
        return true;
    }
    return false;
}

}  // namespace mavlink2::bringup
