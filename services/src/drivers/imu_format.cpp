#include <cmath>
#include <cstring>

#include "services/drivers/imu.hpp"
#include "utils/crc.hpp"

namespace svc {

namespace {

std::uint16_t rd_u16(const std::uint8_t* p, bool be) {
    return be ? static_cast<std::uint16_t>((p[0] << 8) | p[1]) : static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t rd_u32(const std::uint8_t* p, bool be) {
    return be ? (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                    (static_cast<std::uint32_t>(p[2]) << 8) | p[3]
              : (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[2]) << 16) |
                    (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
}
std::int32_t rd_signed(const std::uint8_t* p, std::uint8_t bytes, bool be) {
    return bytes == 4 ? static_cast<std::int32_t>(rd_u32(p, be)) : static_cast<std::int16_t>(rd_u16(p, be));
}

bool checksum_ok(const ImuFormat& f, const std::uint8_t* frame) {
    if (f.checksum == ImuFormat::Checksum::None) return true;
    const std::uint8_t* region = frame + f.checksum_from;
    const std::size_t n        = f.checksum_offset - f.checksum_from;
    if (f.checksum == ImuFormat::Checksum::Crc16X25) {
        const std::uint16_t crc = utils::crc16_x25(region, n);
        return rd_u16(frame + f.checksum_offset, false) == crc;
    }
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i + 1 < n; i += 2) sum += rd_u16(region + i, f.big_endian);
    const auto expect = static_cast<std::uint16_t>(f.checksum == ImuFormat::Checksum::Sum16 ? sum : (0u - sum));
    return rd_u16(frame + f.checksum_offset, f.big_endian) == expect;
}

}  // namespace

ImuDecodeResult decode_imu_frame(const ImuFormat& f, const std::uint8_t* frame, std::size_t len, ImuSample& out) {
    if (frame == nullptr || len < f.frame_len) return ImuDecodeResult::BadLength;
    for (int k = 0; k < 2; ++k) {
        if (f.match_offset[k] >= 0 && frame[f.match_offset[k]] != f.match_value[k]) return ImuDecodeResult::NotDataMessage;
    }
    if (len != f.frame_len) return ImuDecodeResult::BadLength;
    if (!checksum_ok(f, frame)) return ImuDecodeResult::BadChecksum;

    const double scale = f.data == ImuFormat::Data::Increments ? static_cast<double>(f.rate_hz) : 1.0;
    for (int a = 0; a < 3; ++a) {
        out.gyro[a]  = static_cast<float>(rd_signed(frame + f.gyro_offset[a], f.data_bytes, f.big_endian) * f.gyro_lsb * scale);
        out.accel[a] = static_cast<float>(rd_signed(frame + f.accel_offset[a], f.data_bytes, f.big_endian) * f.accel_lsb * scale);
    }
    out.status      = f.status_offset >= 0 ? rd_u16(frame + f.status_offset, f.big_endian) : 0;
    out.temperature = f.temp_offset >= 0
                          ? static_cast<float>(static_cast<std::int16_t>(rd_u16(frame + f.temp_offset, f.big_endian)) * f.temp_lsb)
                          : NAN;
    out.raw_len = static_cast<std::uint8_t>(len < sizeof(out.raw) ? len : sizeof(out.raw));
    std::memcpy(out.raw, frame, out.raw_len);
    return ImuDecodeResult::Ok;
}

namespace imu_formats {

// LN-200 (Northrop Grumman), SDLC 1 Mbit/s, 400 Hz. Word 16-bit (thứ tự byte giả định: little-endian):
//   w0..2 Δv X/Y/Z (2^-14 m/s), w3..5 Δθ X/Y/Z (2^-19 rad), w6 status, w7 mode/MUX ID, w8 MUX data,
//   w9..11 raw gyro, w12 checksum = tổng w0..w11 (mod 2^16).
const ImuFormat kLn200 = {
    "ln200", 1, 26, {-1, -1}, {0, 0}, false, 2,
    {6, 8, 10}, {0, 2, 4},
    0.0000019073486328125 /* 2^-19 */, 0.00006103515625 /* 2^-14 */,
    ImuFormat::Data::Increments, 400.0f,
    12, -1, 0.0f,
    ImuFormat::Checksum::Sum16, 0, 24,
};

// HG1700 (Honeywell), SDLC 1 Mbit/s. Bản tin quán tính 100 Hz:
//   b0 địa chỉ IMU (0x0E), b1 mã bản tin (0x01), Δθ X/Y/Z int32 (2^-33 rad), Δv X/Y/Z int32 (2^-27 ft/s),
//   status u16, checksum u16 = tổng word 16-bit từ byte 2 tới trước checksum.
const ImuFormat kHg1700 = {
    "hg1700", 2, 30, {0, 1}, {0x0E, 0x01}, false, 4,
    {2, 6, 10}, {14, 18, 22},
    1.16415321826934814453125e-10 /* 2^-33 */, 7.4505805969238281e-09 * 0.3048 /* 2^-27 ft/s -> m/s */,
    ImuFormat::Data::Increments, 100.0f,
    26, -1, 0.0f,
    ImuFormat::Checksum::Sum16, 2, 28,
};

}  // namespace imu_formats

}  // namespace svc
