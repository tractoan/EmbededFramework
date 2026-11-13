#pragma once
// Lấy mẫu cảm biến theo kiểu plug-in (đóng với sửa đổi, mở để mở rộng):
//   ISensorTask       – MỘT nguồn dữ liệu: đọc device, đẩy kết quả ra msg::Topic<T>.
//   SensorScheduler   – một thread chạy một NHÓM task, theo dõi sức khỏe từng task, phát
//                       svc::evt::kSensorFault / kSensorRecovered lên EventBus. Không biết loại cảm biến nào.
//   SensorHub         – System giữ; sản phẩm đăng ký các nhóm, System khởi động/dừng chúng.
//
// Hai cơ chế lấy mẫu (ISensorTask::mode()):
//   Periodic : scheduler gọi sample() mỗi period_ms() – lịch không trôi; sample() phải trả về nhanh.
//              Nhiều task định kỳ dùng chung một thread (nhóm).        Adapter: SensorTask<>
//   Blocking : sample() tự CHỜ dữ liệu tối đa period_ms() (timeout) – driver chờ ngắt DRDY, dòng byte
//              UART (GNSS, máy thu RC)... Task chặn chiếm TRỌN thread của nhóm (nhóm chỉ có nó).
//              Hết timeout không có dữ liệu = lỗi (Timeout): N lần liên tiếp -> kSensorFault (dữ liệu cũ).
//                                                                       Adapter: BlockingSensorTask<>,
//                                                                                StreamSensorTask<>
//
//   static msg::Topic<ImuSample> imu_topic;
//   static svc::SensorTask<dev::IImu, ImuSample, &dev::IImu::read> imu{"imu", icm, imu_topic, 1};
//   static svc::SensorScheduler fast;   fast.add(&imu);
//   s.sensors.add(&fast, {"snsr_fast", osal::Priority::Realtime.lower(1), 2048, nullptr});
//
// Nhóm theo bus/nhịp, không theo loại cảm biến: task cùng nhóm chạy TUẦN TỰ trên một thread –
// cảm biến I2C chậm đặt chung với IMU 1 kHz sẽ làm trễ IMU.
// Độ phân giải lịch định kỳ: đồng hồ OSAL (1 ms). Nhanh hơn 1 kHz / jitter < 1 ms: dùng Blocking chờ ngắt DRDY.
#include <array>
#include <atomic>
#include <cstdint>

#include "core/config.hpp"
#include "core/noncopyable.hpp"
#include "core/status.hpp"
#include "hal/uart.hpp"
#include "messaging/event_bus.hpp"
#include "messaging/topic.hpp"
#include "osal/mutex.hpp"
#include "osal/thread.hpp"

#ifndef EP_CFG_MAX_SENSOR_TASKS
#define EP_CFG_MAX_SENSOR_TASKS 8   // mỗi nhóm
#endif
#ifndef EP_CFG_MAX_SENSOR_GROUPS
#define EP_CFG_MAX_SENSOR_GROUPS 4
#endif

namespace svc {

namespace evt {
inline constexpr msg::EventId kSensorFault     = msg::evt::kServiceBase + 2;   // payload: svc::SensorStatusEvent
inline constexpr msg::EventId kSensorRecovered = msg::evt::kServiceBase + 3;   // payload: svc::SensorStatusEvent
}  // namespace evt

struct SensorStatusEvent {
    const char* name;          // tên task (chuỗi tĩnh, sống suốt chương trình)
    core::Status last_error;   // lỗi gần nhất (kSensorFault) / Ok (kSensorRecovered)
};

enum class SamplingMode : std::uint8_t { Periodic, Blocking };

using SensorClock = std::uint64_t (*)();   // thời gian ms (osal::now_ms hoặc đồng hồ giả trong test)

class ISensorTask {
public:
    virtual ~ISensorTask() = default;
    virtual const char* name() const = 0;
    virtual SamplingMode mode() const { return SamplingMode::Periodic; }
    // Periodic: chu kỳ lấy mẫu. Blocking: thời gian chờ dữ liệu tối đa (cũng là độ trễ tối đa khi dừng). > 0.
    virtual std::uint32_t period_ms() const = 0;
    // Đọc device + publish. clock() cho timestamp LÚC CÓ DỮ LIỆU (task chặn: sau khi chờ xong).
    // Lỗi / Timeout -> tính vào sức khỏe; topic giữ giá trị tốt cuối cùng.
    virtual core::Status sample(SensorClock clock) = 0;
};

// Định kỳ: Read là method của Device (có thể là interface, vd. &device::ITemperatureSensor::read_deci_celsius).
template <typename Device, typename Sample, core::Status (Device::*Read)(Sample&)>
class SensorTask final : public ISensorTask {
public:
    SensorTask(const char* name, Device& device, msg::Topic<Sample>& out, std::uint32_t period_ms)
        : name_(name), device_(device), out_(out), period_ms_(period_ms) {}

    const char* name() const override { return name_; }
    std::uint32_t period_ms() const override { return period_ms_; }
    core::Status sample(SensorClock clock) override {
        Sample s{};
        const core::Status st = (device_.*Read)(s);
        if (core::is_ok(st)) out_.publish(s, clock());
        return st;
    }

private:
    const char* name_;
    Device& device_;
    msg::Topic<Sample>& out_;
    std::uint32_t period_ms_;
};

// Chặn: driver có hàm đọc chờ dữ liệu với timeout, vd. chờ semaphore do ISR DRDY give_from_isr().
// Read trả Ok khi có mẫu, Timeout khi hết thời gian chờ.
template <typename Device, typename Sample, core::Status (Device::*Read)(Sample&, std::uint32_t)>
class BlockingSensorTask final : public ISensorTask {
public:
    BlockingSensorTask(const char* name, Device& device, msg::Topic<Sample>& out, std::uint32_t timeout_ms)
        : name_(name), device_(device), out_(out), timeout_ms_(timeout_ms) {}

    const char* name() const override { return name_; }
    SamplingMode mode() const override { return SamplingMode::Blocking; }
    std::uint32_t period_ms() const override { return timeout_ms_; }
    core::Status sample(SensorClock clock) override {
        Sample s{};
        const core::Status st = (device_.*Read)(s, timeout_ms_);
        if (core::is_ok(st)) out_.publish(s, clock());
        return st;
    }

private:
    const char* name_;
    Device& device_;
    msg::Topic<Sample>& out_;
    std::uint32_t timeout_ms_;
};

// Chặn, dòng byte: đọc UART và đưa từng byte vào Parser cho tới khi ra MỘT mẫu hoàn chỉnh hoặc hết timeout.
// Parser (thường ở lớp device: NMEA/UBX, SBUS/CRSF...) chỉ cần:  bool feed(std::uint8_t byte, Sample& out);
//   trả true khi `out` vừa được điền một mẫu hoàn chỉnh; parser tự giữ trạng thái giữa các lần gọi
//   (mẫu bị cắt ngang giữa hai lần đọc vẫn ghép đúng). Nhiều mẫu trong một lần đọc: publish từng mẫu.
template <typename Parser, typename Sample>
class StreamSensorTask final : public ISensorTask {
public:
    static constexpr std::size_t kChunk = 32;   // byte mỗi lần đọc UART (buffer trên stack)

    StreamSensorTask(const char* name, hal::IUart& uart, Parser& parser, msg::Topic<Sample>& out,
                     std::uint32_t timeout_ms)
        : name_(name), uart_(uart), parser_(parser), out_(out), timeout_ms_(timeout_ms) {}

    const char* name() const override { return name_; }
    SamplingMode mode() const override { return SamplingMode::Blocking; }
    std::uint32_t period_ms() const override { return timeout_ms_; }
    core::Status sample(SensorClock clock) override {
        const std::uint64_t deadline = clock() + timeout_ms_;
        bool got                     = false;
        for (std::uint64_t now = clock(); !got; now = clock()) {
            const std::uint32_t left = now < deadline ? static_cast<std::uint32_t>(deadline - now) : 0u;
            std::uint8_t buf[kChunk];
            const std::size_t n = uart_.read(buf, sizeof(buf), left);
            for (std::size_t i = 0; i < n; ++i) {
                Sample s{};
                if (parser_.feed(buf[i], s)) {
                    out_.publish(s, clock());
                    got = true;
                }
            }
            bytes_ += static_cast<std::uint32_t>(n);
            // 0 byte = lần đọc đã chờ hết thời gian còn lại (hoặc UART không chờ được: đóng/lỗi) -> dừng,
            // không quay bận; scheduler nghỉ phần còn lại của timeout nếu trả về sớm.
            if (n == 0 || left == 0) break;
        }
        return got ? core::Status::Ok : core::Status::Timeout;
    }
    std::uint32_t bytes() const { return bytes_; }   // tổng byte đã nhận (chẩn đoán)

private:
    const char* name_;
    hal::IUart& uart_;
    Parser& parser_;
    msg::Topic<Sample>& out_;
    std::uint32_t timeout_ms_;
    std::uint32_t bytes_ = 0;
};

struct SensorHealth {
    const char* name               = nullptr;
    std::uint32_t period_ms        = 0;
    std::uint32_t samples_ok       = 0;
    std::uint32_t errors           = 0;   // tổng số lần đọc lỗi
    std::uint32_t consecutive      = 0;   // lỗi liên tiếp hiện tại
    std::uint32_t overruns         = 0;   // định kỳ: số lần lỡ ít nhất một chu kỳ (lịch bị đặt lại)
    std::uint32_t max_exec_ms      = 0;   // thời gian sample() dài nhất (task chặn: gồm cả thời gian chờ)
    SamplingMode mode              = SamplingMode::Periodic;
    std::uint64_t last_ok_ms       = 0;
    core::Status last_error        = core::Status::Ok;
    bool faulted                   = false;
};

class SensorScheduler : core::NonCopyable {
public:
    using Clock = std::uint64_t (*)();
    struct Config {
        std::uint32_t fault_threshold = 3;   // số lỗi liên tiếp -> kSensorFault
        Clock clock = nullptr;               // nullptr = osal::now_ms (test: đồng hồ giả, tất định)
    };

    SensorScheduler() = default;
    explicit SensorScheduler(Config cfg) : cfg_(cfg) {}
    ~SensorScheduler() { stop(); }

    // Trước start(). Task Blocking phải đứng một mình trong nhóm (InvalidArg nếu trộn).
    core::Status add(ISensorTask* task);
    bool blocking() const { return count_ == 1 && slots_[0].task->mode() == SamplingMode::Blocking; }
    // bus: nơi phát kSensorFault / kSensorRecovered (nullptr = không phát)
    core::Status start(const osal::ThreadConfig& thread, msg::EventBus* bus);
    void stop();
    bool running() const { return running_.load(); }
    // Nơi phát kSensorFault / kSensorRecovered khi dùng run_due() trực tiếp (start() tự gắn).
    void set_event_bus(msg::EventBus* bus) { bus_ = bus; }

    // Một vòng: nhóm định kỳ – chạy mọi task đến hạn (theo Config::clock), trả số ms tới hạn kế tiếp;
    // nhóm chặn – một lần sample() (chờ tối đa timeout của task), trả 0 (hoặc thời gian nghỉ nếu lỗi trả về sớm).
    // Thread của scheduler gọi hàm này; test / super-loop bare-metal gọi trực tiếp (không cần start()).
    std::uint32_t run_due();

    std::size_t count() const { return count_; }
    bool health(std::size_t index, SensorHealth& out) const;   // bản chụp nhất quán (an toàn từ thread khác)

private:
    void run();
    void record(std::size_t i, core::Status st, std::uint64_t now_ms, std::uint32_t exec_ms, bool overrun);
    std::uint64_t now() const;
    SensorClock clock() const;
    std::uint32_t run_blocking_once();

    struct Slot {
        ISensorTask* task       = nullptr;
        std::uint64_t next_due  = 0;
        bool scheduled          = false;
        SensorHealth health{};
    };
    Config cfg_{};
    std::array<Slot, EP_CFG_MAX_SENSOR_TASKS> slots_{};
    std::size_t count_      = 0;
    msg::EventBus* bus_     = nullptr;
    mutable osal::Mutex health_mtx_;   // chỉ bảo vệ Slot::health (đọc từ CLI); giữ rất ngắn
    osal::Thread thread_;
    std::atomic<bool> running_{false};
};

// Các nhóm cảm biến của sản phẩm; System khởi động trước module và dừng sau kênh truyền thông.
class SensorHub : core::NonCopyable {
public:
    core::Status add(SensorScheduler* group, const osal::ThreadConfig& thread);
    core::Status start(msg::EventBus* bus);   // lỗi: dừng các nhóm đã chạy rồi trả lỗi
    void stop();
    std::size_t count() const { return count_; }
    SensorScheduler* at(std::size_t i) const { return i < count_ ? groups_[i].group : nullptr; }
    const char* thread_name(std::size_t i) const { return i < count_ ? groups_[i].thread.name : nullptr; }

private:
    struct Group {
        SensorScheduler* group = nullptr;
        osal::ThreadConfig thread{};
    };
    std::array<Group, EP_CFG_MAX_SENSOR_GROUPS> groups_{};
    std::size_t count_ = 0;
};

}  // namespace svc
