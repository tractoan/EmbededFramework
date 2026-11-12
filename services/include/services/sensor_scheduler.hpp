#pragma once
// Lấy mẫu cảm biến theo kiểu plug-in (đóng với sửa đổi, mở để mở rộng):
//   ISensorTask       – MỘT nguồn dữ liệu: đọc device, đẩy kết quả ra msg::Topic<T>.
//   SensorTask<...>   – adapter tổng quát: một dòng khai báo cho mỗi cảm biến, không cần viết lớp mới.
//   SensorScheduler   – một thread chạy một NHÓM task theo chu kỳ (lịch không trôi), theo dõi sức khỏe
//                       từng task, phát svc::evt::kSensorFault / kSensorRecovered lên EventBus.
//                       Không biết loại cảm biến nào -> thêm cảm biến không phải sửa lớp này.
//   SensorHub         – System giữ; sản phẩm đăng ký các nhóm, System khởi động/dừng chúng.
//
//   static msg::Topic<ImuSample> imu_topic;
//   static svc::SensorTask<dev::IImu, ImuSample, &dev::IImu::read> imu{"imu", icm, imu_topic, 1};
//   static svc::SensorScheduler fast;   fast.add(&imu);
//   s.sensors.add(&fast, {"snsr_fast", osal::Priority::Realtime.lower(1), 2048, nullptr});
//
// Nhóm theo bus/nhịp, không theo loại cảm biến: task cùng nhóm chạy TUẦN TỰ trên một thread –
// cảm biến I2C chậm đặt chung với IMU 1 kHz sẽ làm trễ IMU.
// Độ phân giải: theo đồng hồ OSAL (1 ms). Nhanh hơn 1 kHz / jitter < 1 ms cần ngắt DRDY hoặc timer phần cứng.
#include <array>
#include <atomic>
#include <cstdint>

#include "core/config.hpp"
#include "core/noncopyable.hpp"
#include "core/status.hpp"
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

class ISensorTask {
public:
    virtual ~ISensorTask() = default;
    virtual const char* name() const = 0;
    virtual std::uint32_t period_ms() const = 0;                // > 0
    virtual core::Status sample(std::uint64_t now_ms) = 0;      // đọc device + publish; lỗi -> tính vào sức khỏe
};

// Adapter: Read là method của Device (có thể là interface, vd. &dev::ITemperatureSensor::read_deci_celsius).
template <typename Device, typename Sample, core::Status (Device::*Read)(Sample&)>
class SensorTask final : public ISensorTask {
public:
    SensorTask(const char* name, Device& device, msg::Topic<Sample>& out, std::uint32_t period_ms)
        : name_(name), device_(device), out_(out), period_ms_(period_ms) {}

    const char* name() const override { return name_; }
    std::uint32_t period_ms() const override { return period_ms_; }
    core::Status sample(std::uint64_t now_ms) override {
        Sample s{};
        const core::Status st = (device_.*Read)(s);
        if (core::is_ok(st)) out_.publish(s, now_ms);   // lỗi: giữ giá trị cũ trên topic, sequence không tăng
        return st;
    }

private:
    const char* name_;
    Device& device_;
    msg::Topic<Sample>& out_;
    std::uint32_t period_ms_;
};

struct SensorHealth {
    const char* name               = nullptr;
    std::uint32_t period_ms        = 0;
    std::uint32_t samples_ok       = 0;
    std::uint32_t errors           = 0;   // tổng số lần đọc lỗi
    std::uint32_t consecutive      = 0;   // lỗi liên tiếp hiện tại
    std::uint32_t overruns         = 0;   // số lần lỡ ít nhất một chu kỳ (lịch bị đặt lại)
    std::uint32_t max_exec_ms      = 0;   // thời gian sample() dài nhất
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

    core::Status add(ISensorTask* task);                       // trước start()
    // bus: nơi phát kSensorFault / kSensorRecovered (nullptr = không phát)
    core::Status start(const osal::ThreadConfig& thread, msg::EventBus* bus);
    void stop();
    bool running() const { return running_.load(); }
    // Nơi phát kSensorFault / kSensorRecovered khi dùng run_due() trực tiếp (start() tự gắn).
    void set_event_bus(msg::EventBus* bus) { bus_ = bus; }

    // Một vòng lập lịch: chạy mọi task đến hạn (theo Config::clock), trả về số ms tới hạn kế tiếp.
    // Thread của scheduler gọi hàm này; test / super-loop bare-metal gọi trực tiếp (không cần start()).
    std::uint32_t run_due();

    std::size_t count() const { return count_; }
    bool health(std::size_t index, SensorHealth& out) const;   // bản chụp nhất quán (an toàn từ thread khác)

private:
    void run();
    void record(std::size_t i, core::Status st, std::uint64_t now_ms, std::uint32_t exec_ms, bool overrun);
    std::uint64_t now() const;

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
