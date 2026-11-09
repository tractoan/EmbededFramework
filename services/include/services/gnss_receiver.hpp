#pragma once
// Dịch vụ máy thu GNSS trên một cổng serial (chế độ Stream của ISerialPorts). Thread riêng:
//   - rút byte máy thu từ cổng, giải mã NMEA (device::NmeaParser), gom thành từng fix: một fix mỗi câu
//     GGA (ngày lấy từ RMC gần nhất); máy thu chỉ phát RMC (không GGA trong 2 s) -> một fix mỗi RMC
//   - bản sao byte thô (tap) cho ai cần chuyển tiếp, nếu bật
//   - gửi lệnh tới máy thu / đổi baudrate qua hàng đợi (ghi UART có thể chặn – không chặn người gọi)
// Chỉ là CƠ CHẾ: không có khái niệm thời gian chờ phát hiện, loopback, giao diện... (đó là nghiệp vụ).
// Mọi hàm public thread-safe. Không cấp phát heap (lệnh đi bằng msg::Buffer từ pool tĩnh).
#include <atomic>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "device/nmea.hpp"
#include "messaging/buffer.hpp"
#include "osal/mutex.hpp"
#include "osal/queue.hpp"
#include "osal/thread.hpp"
#include "services/serial_port_bank.hpp"
#include "utils/ring_buffer.hpp"

namespace svc {

struct GnssStats {
    std::uint32_t rx_bytes        = 0;   // byte nhận từ máy thu (kể từ start)
    std::uint32_t tx_bytes        = 0;   // byte lệnh đã ghi
    std::uint32_t sentences       = 0;   // câu NMEA đúng checksum
    std::uint32_t checksum_errors = 0;
    std::uint32_t fixes           = 0;   // số fix đã gom (= seq của latest_fix)
    std::uint32_t raw_dropped     = 0;   // byte thô bỏ do tap đầy (người đọc chậm)
    std::uint32_t tx_dropped      = 0;   // lệnh bỏ do hàng đợi đầy
    std::uint32_t tx_errors       = 0;   // ghi không đủ byte
    std::uint32_t baudrate        = 0;
    std::uint64_t last_rx_ms      = 0;   // 0 = chưa nhận byte nào
    bool running                  = false;
};

class IGnssReceiver {
public:
    virtual ~IGnssReceiver() = default;
    // Đặt cổng sang Stream + baudrate (0 = giá trị trong cấu hình), chạy thread.
    virtual core::Status start(std::uint32_t baudrate = 0) = 0;
    virtual void stop()          = 0;   // dừng thread (không đổi chế độ cổng)
    virtual bool has_data() const = 0;  // đã nhận ít nhất 1 byte kể từ start()
    // Fix mới nhất + số thứ tự (tăng mỗi fix). false nếu chưa có fix nào.
    virtual bool latest_fix(device::GnssFix& out, std::uint32_t& seq) const = 0;
    // Byte thô (một người đọc, không chặn). Chỉ có dữ liệu khi set_raw_tap(true).
    virtual void set_raw_tap(bool enabled)                         = 0;
    virtual std::size_t read_raw(std::uint8_t* data, std::size_t max) = 0;
    // Gửi lệnh tới máy thu; baudrate != 0: đổi tốc độ cổng trước khi gửi (data có thể rỗng).
    // Busy nếu hàng đợi đầy. Không chặn.
    virtual core::Status send(msg::Buffer data, std::uint32_t baudrate = 0) = 0;
    virtual GnssStats stats() const = 0;
};

class GnssReceiver final : public IGnssReceiver, private core::NonCopyable {
public:
    static constexpr std::size_t kTxQueue    = 8;
    static constexpr std::size_t kRawTap     = 4096;
    static constexpr std::uint32_t kRmcOnlyMs = 2000;   // không thấy GGA lâu hơn -> gom fix theo RMC

    struct Config {
        ISerialPorts* ports     = nullptr;
        std::size_t port        = 0;
        std::uint32_t baudrate  = 115200;
        osal::ThreadConfig task{"gnss", osal::Priority::Normal, 2048, nullptr};
    };

    core::Status init(const Config& cfg);

    core::Status start(std::uint32_t baudrate = 0) override;
    void stop() override;
    bool has_data() const override { return rx_bytes_.load() > 0; }
    bool latest_fix(device::GnssFix& out, std::uint32_t& seq) const override;
    void set_raw_tap(bool enabled) override { tap_ = enabled; }
    std::size_t read_raw(std::uint8_t* data, std::size_t max) override { return raw_.pop(data, max); }
    core::Status send(msg::Buffer data, std::uint32_t baudrate = 0) override;
    GnssStats stats() const override;

    // Một vòng xử lý (public để test không cần thread): ghi tối đa 1 lệnh, rút & giải mã byte nhận.
    void process(std::uint32_t wait_ms, std::uint64_t now_ms);

private:
    struct TxJob {
        msg::Buffer data;
        std::uint32_t baudrate = 0;
    };
    void run();
    void on_sentence(const device::GnssFix& fix, device::NmeaParser::Result kind, std::uint64_t now_ms);

    Config cfg_{};
    osal::Thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> tap_{false};

    device::NmeaParser parser_;   // chỉ thread dịch vụ
    std::uint64_t last_gga_ms_ = 0;

    mutable osal::Mutex fix_mtx_;
    device::GnssFix fix_{};
    std::uint32_t fix_seq_ = 0;

    osal::Queue<TxJob, kTxQueue> tx_;
    utils::RingBuffer<std::uint8_t, kRawTap> raw_;   // thread dịch vụ -> người đọc
    std::atomic<std::uint32_t> rx_bytes_{0}, tx_bytes_{0}, sentences_{0}, checksum_errors_{0}, raw_dropped_{0},
        tx_dropped_{0}, tx_errors_{0}, baud_{0};
    std::atomic<std::uint64_t> last_rx_ms_{0};
};

}  // namespace svc
