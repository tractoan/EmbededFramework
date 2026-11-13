#pragma once
// Actuator tự chủ: mỗi bộ chấp hành (servo, ESC, relay, stepper, heater...) là một thành phần TỰ xử lý
// phần cứng của nó – có thể có THREAD RIÊNG (vd. servo tự chạy vòng PID vị trí, ESC tự làm mới khung,
// stepper tự tạo ramp). Bên ngoài (nghiệp vụ, flight stack, giao thức) chỉ gọi command().
//
//   class Servo final : public svc::Actuator<ServoCommand> {
//   public:
//       Servo(...) : Actuator("servo", {/*update_period_ms*/ 2, /*command_timeout_ms*/ 200}) {}
//   private:
//       void on_update(const ServoCommand& sp, std::uint64_t now) override {   // thread riêng, mỗi 2 ms
//           const float pos = feedback_.read_deg();
//           pwm_.set_duty(pid_.update(sp.deg - pos, 0.002f));               // PID là việc NỘI BỘ của servo
//       }
//       void write_safe() override { pwm_.set_duty(0); }                    // trạng thái an toàn
//   };
//
// Hai chế độ (Config::update_period_ms):
//   0  : TRỰC TIẾP – command() ghi phần cứng ngay trên thread người gọi (on_command). Relay, servo RC...
//   >0 : THREAD RIÊNG – command() chỉ lưu setpoint mới nhất (không chặn); thread của actuator gọi
//        on_update(setpoint mới nhất) theo lịch không trôi. Vòng điều khiển, làm mới định kỳ, ramp.
//
// An toàn (chung cho mọi actuator, lớp con không phải tự làm):
//   - Chỉ xuất khi đã arm(); chưa arm / disarm / tắt máy -> write_safe().
//   - Deadman: không có lệnh mới quá command_timeout_ms -> về an toàn; lệnh mới -> tự xuất lại.
//   - Thread riêng bị KẸT quá stall_timeout_ms -> ActuatorHub (thread giám sát độc lập) ép về an toàn và
//     disarm (chốt: phải arm lại). Kiểm tra này chạy được cả khi chính thread của actuator đã treo.
//   - Ghi phần cứng (on_command / on_update / write_safe) luôn tuần tự hóa bằng khóa riêng của actuator;
//     sau khi đã ép về an toàn, actuator không xuất giá trị nào khác cho tới khi được phép lại.
//   - Phát svc::evt::kActuatorSafe / kActuatorResumed lên EventBus.
#include <array>
#include <atomic>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "core/status.hpp"
#include "messaging/event_bus.hpp"
#include "messaging/topic.hpp"
#include "osal/mutex.hpp"
#include "osal/thread.hpp"
#include "services/periodic_schedule.hpp"

#ifndef EP_CFG_MAX_ACTUATORS
#define EP_CFG_MAX_ACTUATORS 8
#endif

namespace svc {

namespace evt {
inline constexpr msg::EventId kActuatorSafe    = msg::evt::kServiceBase + 4;   // payload: svc::ActuatorEvent
inline constexpr msg::EventId kActuatorResumed = msg::evt::kServiceBase + 5;   // payload: svc::ActuatorEvent
}  // namespace evt

// Vì sao actuator đang ở trạng thái an toàn.
enum class SafeReason : std::uint8_t {
    None = 0,          // đang xuất bình thường
    NotArmed,          // chưa arm / đã disarm
    NoCommand,         // đã arm nhưng chưa có lệnh nào
    CommandTimeout,    // deadman: lệnh quá hạn (lệnh mới -> tự xuất lại)
    Stalled,           // thread riêng bị kẹt (chốt: cần arm lại)
    Shutdown,          // hệ thống dừng
};
const char* to_string(SafeReason r);

struct ActuatorEvent {
    const char* name;
    SafeReason reason;
};

struct ActuatorStatus {
    const char* name           = nullptr;
    bool threaded              = false;
    bool armed                 = false;
    SafeReason reason          = SafeReason::NotArmed;
    std::uint32_t period_ms    = 0;
    std::uint32_t commands     = 0;   // lệnh được nhận
    std::uint32_t rejected     = 0;   // lệnh bị từ chối (chưa arm / bị chốt)
    std::uint32_t updates      = 0;   // số lần on_update (thread riêng)
    std::uint32_t overruns     = 0;   // thread riêng lỡ ít nhất một chu kỳ
    std::uint32_t safe_events  = 0;   // số lần bị ép về an toàn
};

using ActuatorClock = std::uint64_t (*)();

// Phần không phụ thuộc kiểu lệnh: vòng đời, an toàn, thread. ActuatorHub làm việc qua lớp này.
class ActuatorBase : core::NonCopyable {
public:
    struct Config {
        std::uint32_t update_period_ms   = 0;        // 0 = trực tiếp; >0 = thread riêng, chu kỳ on_update
        std::uint32_t command_timeout_ms = 0;        // deadman; 0 = tắt
        std::uint32_t stall_timeout_ms   = 0;        // thread riêng; 0 = max(4 × chu kỳ, 20 ms)
        ActuatorClock clock              = nullptr;  // nullptr = osal::now_ms (test: đồng hồ giả)
    };

    // Phải stop() (ActuatorHub / System làm) TRƯỚC khi hủy: destructor chạy sau khi lớp con đã bị hủy,
    // thread riêng còn chạy lúc đó sẽ gọi hàm ảo của đối tượng đã chết. Destructor chỉ join thread.
    virtual ~ActuatorBase();
    const char* name() const { return name_; }
    bool threaded() const { return cfg_.update_period_ms > 0; }
    const Config& config() const { return cfg_; }

    void arm();      // cho phép xuất (xóa cả chốt Stalled); chờ lệnh đầu tiên
    void disarm();   // về an toàn ngay
    bool armed() const { return armed_.load(); }
    SafeReason reason() const { return static_cast<SafeReason>(reason_.load()); }
    ActuatorStatus status() const;

    // Ép về an toàn ngay, từ bất kỳ thread nào (supervisor, failsafe của nghiệp vụ...).
    void force_safe(SafeReason why);

    // ---- do ActuatorHub / System gọi
    void write_safe_now();                                                    // lúc khởi động, trước mọi thứ
    core::Status start(const osal::ThreadConfig& thread, msg::EventBus* bus); // thread chỉ dùng khi threaded()
    void stop();                                                              // dừng thread + về an toàn
    void supervise(std::uint64_t now);   // deadman + phát hiện thread kẹt (thread giám sát gọi định kỳ)
    void step(std::uint64_t now);        // một lần cập nhật của thread riêng (test / super-loop gọi trực tiếp)
    void set_event_bus(msg::EventBus* bus) { bus_ = bus; }

protected:
    ActuatorBase(const char* name, const Config& cfg);
    std::uint64_t now() const;

    virtual void write_safe() = 0;   // lớp con: ghi trạng thái an toàn ra phần cứng (idempotent)
    // Gọi bởi Actuator<Command>: true nếu đã có lệnh -> chạy on_update với lệnh mới nhất.
    virtual bool update_with_latest(std::uint64_t now) = 0;

    // Dùng bởi Actuator<Command>::command()
    bool accept_command(std::uint64_t now);   // false: chưa arm / bị chốt -> từ chối
    bool output_allowed() const { return armed_.load() && reason() == SafeReason::None; }
    osal::Mutex& io_mutex() { return io_mtx_; }

private:
    void run();
    void publish(msg::EventId id, SafeReason r);
    bool set_reason(SafeReason from, SafeReason to);
    std::uint32_t stall_timeout() const;

    const char* name_;
    Config cfg_;
    std::atomic<bool> armed_{false};
    std::atomic<std::uint8_t> reason_{static_cast<std::uint8_t>(SafeReason::NotArmed)};
    std::atomic<std::uint32_t> last_command_ms_{0};   // 32 bit: không cần atomic 64 bit (không lock-free trên MCU)
    std::atomic<std::uint32_t> last_update_ms_{0};
    std::atomic<std::uint32_t> commands_{0}, rejected_{0}, updates_{0}, overruns_{0}, safe_events_{0};
    msg::EventBus* bus_ = nullptr;
    osal::Mutex io_mtx_;               // tuần tự hóa mọi lần ghi phần cứng
    osal::Thread thread_;
    std::atomic<bool> running_{false};
    PeriodicSchedule schedule_;
};

// Actuator có kiểu lệnh. Command: trivially copyable (vd. struct { float deg; }).
template <typename Command>
class Actuator : public ActuatorBase {
public:
    // Thread-safe, không chặn lâu. Ok: đã nhận; Busy: chưa arm / bị chốt (lệnh bị bỏ).
    core::Status command(const Command& cmd) {
        const std::uint64_t t = now();
        if (!accept_command(t)) return core::Status::Busy;
        {
            osal::LockGuard g(cmd_mtx_);   // Topic chỉ cho một bên ghi: nhiều nguồn lệnh -> tuần tự hóa
            latest_.publish(cmd, t);
        }
        if (!threaded()) {   // trực tiếp: ghi phần cứng ngay trên thread người gọi
            osal::LockGuard g(io_mutex());
            if (output_allowed()) on_command(cmd, t);
        }
        return core::Status::Ok;
    }
    // Lệnh mới nhất (vd. để báo cáo telemetry).
    core::Status last_command(Command& out) const { return latest_.read(out); }

protected:
    Actuator(const char* name, const Config& cfg) : ActuatorBase(name, cfg) {}

    // Chế độ trực tiếp: ghi phần cứng cho lệnh vừa nhận.
    virtual void on_command(const Command& /*cmd*/, std::uint64_t /*now*/) {}
    // Chế độ thread riêng: mỗi chu kỳ, với setpoint mới nhất (vòng điều khiển, làm mới, ramp...).
    virtual void on_update(const Command& /*latest*/, std::uint64_t /*now*/) {}

private:
    bool update_with_latest(std::uint64_t t) final {
        Command c{};
        const core::Status st = latest_.read(c);
        if (st == core::Status::NotFound) return false;
        if (!core::is_ok(st)) return true;   // bên ghi đang ghi dở: bỏ qua chu kỳ này, giữ output cũ
        on_update(c, t);
        return true;
    }

    msg::Topic<Command> latest_;
    osal::Mutex cmd_mtx_;
};

// Các actuator của sản phẩm + thread giám sát độc lập. System: write_safe_all() ngay sau khi sản phẩm
// khai báo, start() sau cảm biến, stop() ĐẦU TIÊN khi tắt máy.
class ActuatorHub : core::NonCopyable {
public:
    // thread: priority/stack cho thread riêng của actuator (bỏ qua nếu actuator trực tiếp)
    core::Status add(ActuatorBase* actuator, const osal::ThreadConfig& thread = {"act", osal::Priority::High, 2048, nullptr});
    void write_safe_all();
    // supervisor phải có priority CAO HƠN mọi thread actuator (nếu không: thread kẹt ở priority cao sẽ
    // không bao giờ để supervisor chạy) – start() cảnh báo nếu không thỏa.
    core::Status start(msg::EventBus* bus, const osal::ThreadConfig& supervisor, std::uint32_t supervise_period_ms = 5);
    void stop();
    void supervise_all(std::uint64_t now);   // một vòng giám sát (thread giám sát / test)
    void disarm_all();

    std::size_t count() const { return count_; }
    ActuatorBase* at(std::size_t i) const { return i < count_ ? slots_[i].actuator : nullptr; }

private:
    void run();
    struct Slot {
        ActuatorBase* actuator = nullptr;
        osal::ThreadConfig thread{};
    };
    std::array<Slot, EP_CFG_MAX_ACTUATORS> slots_{};
    std::size_t count_ = 0;
    std::uint32_t period_ms_ = 5;
    osal::Thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace svc
