#pragma once
// Dịch vụ quản lý một dãy cổng serial (RS232/RS485) cho bring-up/kiểm tra sản xuất.
// Mỗi cổng có một thread riêng và một chế độ, đổi được lúc chạy:
//   Off       : không dùng
//   Loopback  : định kỳ gửi khung thử (có số thứ tự + CRC) và chờ nhận lại đúng khung đó
//               (giắc TX->RX trên chính cổng, hoặc nối chéo với một cổng "peer")
//   Stream    : thiết bị ngoài (vd. máy thu GNSS): byte nhận vào ring buffer, ghi được từ thread khác
//   External  : cổng do thành phần khác sở hữu (vd. kênh MAVLink tới phần mềm giao diện) – không đụng tới
#include <array>
#include <atomic>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "hal/uart.hpp"
#include "osal/thread.hpp"
#include "utils/ring_buffer.hpp"

namespace svc {

enum class SerialMode : std::uint8_t { Off = 0, Loopback, Stream, External };
enum class LoopResult : std::uint8_t { None = 0, Pass, Timeout, Mismatch, OpenError };

const char* to_string(SerialMode m);
const char* to_string(LoopResult r);

struct SerialPortStats {
    SerialMode mode       = SerialMode::Off;
    LoopResult last       = LoopResult::None;
    std::uint32_t pass    = 0;
    std::uint32_t fail    = 0;
    std::uint32_t tx_bytes = 0;
    std::uint32_t rx_bytes = 0;
    std::uint32_t overflow = 0;   // Stream: byte bị mất do ring buffer đầy
    std::uint32_t baudrate = 0;
    std::uint64_t last_rx_ms = 0;
    std::int8_t peer       = -1;
};

class ISerialPorts {
public:
    virtual ~ISerialPorts() = default;
    virtual std::size_t count() const = 0;
    virtual core::Status set_mode(std::size_t port, SerialMode mode) = 0;
    virtual SerialPortStats stats(std::size_t port) const          = 0;
    virtual void reset_counters(std::size_t port)                   = 0;
    // Chế độ Stream:
    virtual std::size_t write(std::size_t port, const std::uint8_t* data, std::size_t len) = 0;
    virtual std::size_t read(std::size_t port, std::uint8_t* data, std::size_t max)        = 0;   // không chặn
    virtual core::Status set_baudrate(std::size_t port, std::uint32_t baud)                  = 0;
    // Loopback: chu kỳ một lần thử và thời gian chờ phản hồi (cộng thêm thời gian truyền khung).
    virtual void set_test_timing(std::uint32_t period_ms, std::uint32_t timeout_ms) = 0;
    virtual core::Status start() = 0;
    virtual void stop()          = 0;
};

class SerialPortBank final : public ISerialPorts, private core::NonCopyable {
public:
    static constexpr std::size_t kMaxPorts       = 16;
    static constexpr std::size_t kFrameLen       = 18;   // 55 AA port seq + 12 byte mẫu + CRC16
    static constexpr std::size_t kStreamBuffer   = 4096;

    struct PortConfig {
        hal::IUart* uart = nullptr;            // nullptr: cổng không có trên board -> luôn OpenError
        hal::UartConfig uart_cfg{};
        SerialMode mode = SerialMode::Off;     // chế độ ban đầu
        std::int8_t peer = -1;                 // Loopback nối chéo với cổng này (-1: giắc trên chính cổng)
        const char* name = "ser";              // tên thread
    };

    core::Status add(const PortConfig& cfg);

    std::size_t count() const override { return count_; }
    core::Status set_mode(std::size_t port, SerialMode mode) override;
    SerialPortStats stats(std::size_t port) const override;
    void reset_counters(std::size_t port) override;
    std::size_t write(std::size_t port, const std::uint8_t* data, std::size_t len) override;
    std::size_t read(std::size_t port, std::uint8_t* data, std::size_t max) override;
    core::Status set_baudrate(std::size_t port, std::uint32_t baud) override;
    void set_test_timing(std::uint32_t period_ms, std::uint32_t timeout_ms) override {
        period_ms_  = period_ms;
        timeout_ms_ = timeout_ms;
    }
    core::Status start() override;
    void stop() override;

    // Dựng khung thử (public để test).
    static void make_test_frame(std::uint8_t port, std::uint8_t seq, std::uint8_t* out);

private:
    struct Port : core::NonCopyable {
        void run();
        bool ensure_open();
        void loopback_once(Port& rx_side, Port& stats_side);
        void stream_once();

        SerialPortBank* bank = nullptr;
        std::uint8_t index   = 0;
        PortConfig cfg{};
        osal::Thread thread;
        std::atomic<SerialMode> mode{SerialMode::Off};
        std::atomic<std::uint32_t> baud_request{0};
        bool is_open = false;
        std::uint64_t retry_open_ms = 0;
        std::uint8_t seq = 0;
        bool pair_dir = false;   // nối chéo: luân phiên chiều A->B / B->A

        std::atomic<LoopResult> last{LoopResult::None};
        std::atomic<std::uint32_t> pass{0}, fail{0}, tx_bytes{0}, rx_bytes{0}, overflow{0}, baud{0};
        std::atomic<std::uint64_t> last_rx_ms{0};
        utils::RingBuffer<std::uint8_t, kStreamBuffer> rx;   // Stream: thread cổng -> thread đọc
    };

    std::array<Port, kMaxPorts> ports_;
    std::size_t count_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<std::uint32_t> period_ms_{200};
    std::atomic<std::uint32_t> timeout_ms_{50};
};

}  // namespace svc
