#pragma once
// Interface thiết bị IMU + bộ giải mã khung theo BẢNG ĐỊNH DẠNG (table-driven).
// Thêm một loại IMU mới = khai báo một ImuFormat (vị trí trường, hệ số LSB, checksum),
// không phải viết driver mới.
#include <cstddef>
#include <cstdint>

#include "core/status.hpp"

namespace device {

struct ImuSample {
    std::uint32_t time_ms   = 0;     // thời điểm nhận khung (osal::now_ms)
    float gyro[3]           = {};    // rad/s
    float accel[3]          = {};    // m/s²
    float temperature       = 0;     // °C (NaN nếu định dạng không có)
    std::uint16_t status    = 0;     // status word thô của IMU
    std::uint8_t raw_len    = 0;     // số byte thô hợp lệ trong raw
    std::uint8_t raw[48]    = {};    // đầu khung thô (để đối chiếu ICD khi bring-up)
};

struct ImuStats {
    std::uint32_t frames        = 0;   // khung hợp lệ
    std::uint32_t crc_errors    = 0;   // FCS sai (phần cứng báo hoặc kiểm tra phần mềm)
    std::uint32_t format_errors = 0;   // sai độ dài / byte nhận dạng / checksum của bản tin
    std::uint32_t other_frames  = 0;   // khung đúng FCS nhưng không phải bản tin dữ liệu (vd. bản tin khác ID)
    std::uint32_t rate_hz       = 0;   // tần số khung hợp lệ đo được (cửa sổ 1 s)
    std::uint64_t last_frame_ms = 0;   // 0 = chưa nhận khung nào
};

// Hiện thực có thể chạy thread riêng (vd. svc::HdlcImu) – device chỉ định nghĩa interface + bộ giải mã.
class IImu {
public:
    virtual ~IImu() = default;
    virtual const char* format_name() const = 0;
    virtual std::uint8_t type_id() const    = 0;
    virtual core::Status start()            = 0;
    virtual void stop()                     = 0;
    virtual bool latest(ImuSample& out) const = 0;   // false nếu chưa có mẫu
    virtual ImuStats stats() const          = 0;
};

// ----------------------------------------------------------------- định dạng
struct ImuFormat {
    enum class Data : std::uint8_t { Increments, Rates };   // Increments: Δθ [rad], Δv [m/s] mỗi khung
    enum class Checksum : std::uint8_t { None, Sum16, NegSum16, Crc16X25 };

    const char* name;
    std::uint8_t type_id;              // gửi lên phần mềm giao diện
    std::uint16_t frame_len;           // độ dài khung dữ liệu (không gồm FCS HDLC)
    std::int16_t match_offset[2];      // byte nhận dạng (vd. địa chỉ, mã bản tin); <0 = bỏ qua
    std::uint8_t match_value[2];
    bool big_endian;                   // thứ tự byte của word
    std::uint8_t data_bytes;           // 2 hoặc 4: kích thước số có dấu của gyro/accel
    std::uint16_t gyro_offset[3];      // byte offset X, Y, Z
    std::uint16_t accel_offset[3];
    double gyro_lsb;                   // rad/LSB (Increments) hoặc rad/s/LSB (Rates)
    double accel_lsb;                  // m/s/LSB (Increments) hoặc m/s²/LSB (Rates)
    Data data;
    float rate_hz;                     // tần số danh định: đổi gia số -> tốc độ
    std::int16_t status_offset;        // u16; <0 = không có
    std::int16_t temp_offset;          // i16; <0 = không có
    float temp_lsb;                    // °C/LSB
    Checksum checksum;
    std::uint16_t checksum_from;       // vùng tính [from, checksum_offset)
    std::uint16_t checksum_offset;     // vị trí u16 checksum
};

enum class ImuDecodeResult : std::uint8_t { Ok, BadLength, NotDataMessage, BadChecksum };

// Hàm thuần (không phụ thuộc phần cứng) -> unit test được bằng khung mẫu.
ImuDecodeResult decode_imu_frame(const ImuFormat& fmt, const std::uint8_t* frame, std::size_t len, ImuSample& out);

// Định dạng tham khảo cho 2 loại IMU SDLC phổ biến.
// LƯU Ý: dựng theo tài liệu công khai; PHẢI đối chiếu ICD của đúng phiên bản IMU
// (thứ tự byte, vị trí trường, hệ số LSB, checksum) trước khi dùng số liệu đã giải mã.
// Trong lúc bring-up, khung thô (ImuSample::raw) luôn được gửi kèm để đối chiếu.
namespace imu_formats {
extern const ImuFormat kLn200;    // 13 word 16-bit: Δv xyz 2^-14 m/s, Δθ xyz 2^-19 rad, ..., checksum; 400 Hz
extern const ImuFormat kHg1700;   // địa chỉ + mã bản tin, Δθ xyz 2^-33 rad, Δv xyz 2^-27 ft/s (int32); 100 Hz
}  // namespace imu_formats

}  // namespace device
