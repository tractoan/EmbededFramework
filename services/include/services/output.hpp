#pragma once
// Output: mọi thứ hệ thống đưa RA phần cứng – mỗi output là một thành phần TỰ xử lý phần cứng của nó và có
// thể có THREAD RIÊNG (servo tự chạy PID vị trí, ESC tự làm mới khung, LED tự tạo mẫu nhấp nháy...).
// Bên ngoài (nghiệp vụ, flight stack, giao thức) chỉ gọi command(). OutputHub đăng ký, quản lý vòng đời và
// giám sát MỌI output của hệ thống (kể cả LED – xem services/led_service.hpp).
//
//   class Servo final : public svc::Output<ServoCommand> {
//   public:
//       Servo(...) : Output("servo", {/*update_period_ms*/ 2, /*command_timeout_ms*/ 200}) {}
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
//   >0 : THREAD RIÊNG – command() chỉ lưu lệnh mới nhất (không chặn); thread của output gọi
//        on_update(lệnh mới nhất) theo lịch không trôi. Vòng điều khiển, làm mới định kỳ, ramp, mẫu LED.
//
// Hai loại theo mức an toàn (Config::cls):
//   Actuator  : tác động lên quá trình vật lý, có thể nguy hiểm (motor, servo, van, heater, relay tải):
//               phải arm(); deadman; thread kẹt -> ép an toàn + CHỐT (phải arm lại); disarm_all() tắt.
//   Indicator : chỉ báo cho người (LED, còi): luôn được xuất (không cần arm); disarm_all() / failsafe
//               KHÔNG tắt (không được tắt đèn cảnh báo đúng lúc có sự cố); không giám sát kẹt.
//   Chung: an toàn lúc khởi động (sau product::setup) và khi tắt máy; deadman nếu cấu hình.
//
// Đảm bảo khi ghi phần cứng: on_command / on_update / write_safe luôn tuần tự hóa bằng khóa riêng của output;
// sau khi force_safe() trả về, output không xuất giá trị nào khác cho tới khi được phép lại.
// Sự kiện: svc::evt::kOutputSafe (kèm lý do) / kOutputResumed lên EventBus.
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

#ifndef EP_CFG_MAX_OUTPUTS
#define EP_CFG_MAX_OUTPUTS 8
#endif

namespace svc {

namespace evt {
inline constexpr msg::EventId kOutputSafe    = msg::evt::kServiceBase + 4;   // payload: svc::OutputEvent
inline constexpr msg::EventId kOutputResumed = msg::evt::kServiceBase + 5;   // payload: svc::OutputEvent
}  // namespace evt

// Mức an toàn của output.
enum class OutputClass : std::uint8_t {
    Actuator,    // tác động vật lý, có thể nguy hiểm: arm, deadman, chốt khi kẹt, tắt khi failsafe
    Indicator,   // chỉ báo (LED, còi): không cần arm, không bị failsafe tắt, không giám sát kẹt
};
const char* to_string(OutputClass c);

// Vì sao output đang ở trạng thái an toàn.
enum class SafeReason : std::uint8_t {
    None = 0,          // đang xuất bình thường
    NotArmed,          // chưa arm / đã disarm
    NoCommand,         // đã arm nhưng chưa có lệnh nào
    CommandTimeout,    // deadman: lệnh quá hạn (lệnh mới -> tự xuất lại)
    Stalled,           // thread riêng bị kẹt (chốt: cần arm lại)
    Shutdown,          // hệ thống dừng
};
const char* to_string(SafeReason r);

struct OutputEvent {
    const char* name;
    SafeReason reason;
};

struct OutputStatus {
    const char* name           = nullptr;
    OutputClass cls            = OutputClass::Actuator;
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

using OutputClock = std::uint64_t (*)();

// Phần không phụ thuộc kiểu lệnh: vòng đời, an toàn, thread. OutputHub làm việc qua lớp này.
class OutputBase : core::NonCopyable {
public:
    struct Config {
        std::uint32_t update_period_ms   = 0;        // 0 = trực tiếp; >0 = thread riêng, chu kỳ on_update
        std::uint32_t command_timeout_ms = 0;        // deadman; 0 = tắt
        std::uint32_t stall_timeout_ms   = 0;        // thread riêng; 0 = max(4 × chu kỳ, 20 ms)
        OutputClock clock                = nullptr;  // nullptr = osal::now_ms (test: đồng hồ giả)
        OutputClass cls                  = OutputClass::Actuator;
    };

    // Phải stop() (OutputHub / System làm) TRƯỚC khi hủy: destructor chạy sau khi lớp con đã bị hủy,
    // thread riêng còn chạy lúc đó sẽ gọi hàm ảo của đối tượng đã chết. Destructor chỉ join thread.
    virtual ~OutputBase();
    const char* name() const { return name_; }
    bool threaded() const { return cfg_.update_period_ms > 0; }
    OutputClass output_class() const { return cfg_.cls; }
    // Cần thread giám sát của OutputHub: actuator (phát hiện kẹt) hoặc có deadman.
    bool needs_supervision() const { return cfg_.cls == OutputClass::Actuator || cfg_.command_timeout_ms != 0; }
    const Config& config() const { return cfg_; }

    void arm();      // cho phép xuất (xóa cả chốt Stalled); chờ lệnh đầu tiên. Indicator: đã arm sẵn
    void disarm();   // về an toàn ngay (gọi trực tiếp: áp dụng cho cả indicator)
    bool armed() const { return armed_.load(); }
    SafeReason reason() const { return static_cast<SafeReason>(reason_.load()); }
    OutputStatus status() const;

    // Ép về an toàn ngay, từ bất kỳ thread nào (supervisor, failsafe của nghiệp vụ...).
    void force_safe(SafeReason why);

    // ---- do OutputHub / System gọi
    void write_safe_now();                                                    // lúc khởi động, trước mọi thứ
    core::Status start(const osal::ThreadConfig& thread, msg::EventBus* bus); // thread chỉ dùng khi threaded()
    void stop();                                                              // dừng thread + về an toàn
    void supervise(std::uint64_t now);   // deadman + phát hiện thread kẹt (thread giám sát gọi định kỳ)
    void step(std::uint64_t now);        // một lần cập nhật của thread riêng (test / super-loop gọi trực tiếp)
    void set_event_bus(msg::EventBus* bus) { bus_ = bus; }

protected:
    OutputBase(const char* name, const Config& cfg);
    std::uint64_t now() const;

    virtual void write_safe() = 0;   // lớp con: ghi trạng thái an toàn ra phần cứng (idempotent)
    // Gọi bởi Output<Command>: true nếu đã có lệnh -> chạy on_update với lệnh mới nhất.
    virtual bool update_with_latest(std::uint64_t now) = 0;

    // Dùng bởi Output<Command>::command()
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

// Output có kiểu lệnh. Command: trivially copyable (vd. struct { float deg; }).
template <typename Command>
class Output : public OutputBase {
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
    Output(const char* name, const Config& cfg) : OutputBase(name, cfg) {}

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

// Mọi output của hệ thống + thread giám sát độc lập. System: write_safe_all() ngay sau khi sản phẩm khai báo,
// start() sau cảm biến, stop() ĐẦU TIÊN khi tắt máy. Thread giám sát chỉ được tạo khi có output cần giám sát
// (needs_supervision()) – sản phẩm chỉ có indicator (LED) không tốn thêm thread.
class OutputHub : core::NonCopyable {
public:
    // thread: priority/stack cho thread riêng của output (bỏ qua nếu output trực tiếp)
    core::Status add(OutputBase* output, const osal::ThreadConfig& thread = {"out", osal::Priority::High, 2048, nullptr});
    void write_safe_all();
    // supervisor phải có priority CAO HƠN mọi thread actuator (nếu không: thread kẹt ở priority cao sẽ
    // không bao giờ để supervisor chạy) – start() cảnh báo nếu không thỏa.
    core::Status start(msg::EventBus* bus, const osal::ThreadConfig& supervisor, std::uint32_t supervise_period_ms = 5);
    void stop();
    void supervise_all(std::uint64_t now);   // một vòng giám sát (thread giám sát / test)
    void disarm_all();   // failsafe: mọi ACTUATOR về an toàn; indicator giữ nguyên
    bool supervising() const { return running_.load(); }

    std::size_t count() const { return count_; }
    OutputBase* at(std::size_t i) const { return i < count_ ? slots_[i].output : nullptr; }

private:
    void run();
    struct Slot {
        OutputBase* output = nullptr;
        osal::ThreadConfig thread{};
    };
    std::array<Slot, EP_CFG_MAX_OUTPUTS> slots_{};
    std::size_t count_ = 0;
    std::uint32_t period_ms_ = 5;
    osal::Thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace svc
