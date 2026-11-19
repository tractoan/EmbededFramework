#include "services/output.hpp"

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace svc {

namespace {
constexpr const char* kTag          = "output";
constexpr std::uint32_t kPollMs     = 50;   // ngủ tối đa mỗi lát để kịp thấy yêu cầu dừng
constexpr int kIoLockTries          = 5;    // force_safe chờ khóa phần cứng tối đa ~5 ms (5 lần x 1 ms)
constexpr std::uint32_t kMinStallMs = 20;

std::uint32_t ms32(std::uint64_t t) { return static_cast<std::uint32_t>(t); }
// Hiệu thời gian 32 bit, đúng cả khi bộ đếm quay vòng (~49 ngày).
std::uint32_t elapsed(std::uint64_t now, std::uint32_t since) { return ms32(now) - since; }
}  // namespace

const char* to_string(OutputClass c) { return c == OutputClass::Indicator ? "indicator" : "actuator"; }

const char* to_string(SafeReason r) {
    switch (r) {
        case SafeReason::None:           return "active";
        case SafeReason::NotArmed:       return "disarmed";
        case SafeReason::NoCommand:      return "no-command";
        case SafeReason::CommandTimeout: return "cmd-timeout";
        case SafeReason::Stalled:        return "STALLED";
        case SafeReason::Shutdown:       return "shutdown";
    }
    return "?";
}

OutputBase::OutputBase(const char* name, const Config& cfg) : name_(name), cfg_(cfg) {
    if (cfg_.cls == OutputClass::Indicator) {   // chỉ báo: luôn được xuất, chờ lệnh đầu tiên
        armed_  = true;
        reason_ = static_cast<std::uint8_t>(SafeReason::NoCommand);
    }
}

OutputBase::~OutputBase() {
    running_ = false;
    thread_.join();
}

std::uint64_t OutputBase::now() const { return cfg_.clock != nullptr ? cfg_.clock() : osal::now_ms(); }

std::uint32_t OutputBase::stall_timeout() const {
    if (cfg_.stall_timeout_ms != 0) return cfg_.stall_timeout_ms;
    const std::uint32_t t = cfg_.update_period_ms * 4u;
    return t > kMinStallMs ? t : kMinStallMs;
}

bool OutputBase::set_reason(SafeReason from, SafeReason to) {
    auto expected = static_cast<std::uint8_t>(from);
    return reason_.compare_exchange_strong(expected, static_cast<std::uint8_t>(to));
}

void OutputBase::publish(msg::EventId id, SafeReason r) {
    if (bus_ != nullptr) bus_->publish(msg::Event::make(id, OutputEvent{name_, r}));
}

void OutputBase::arm() {
    last_command_ms_ = ms32(now());   // deadman tính từ lúc arm
    reason_          = static_cast<std::uint8_t>(SafeReason::NoCommand);
    armed_           = true;
    LOGI(kTag, "'%s' armed", name_);
}

void OutputBase::disarm() {
    armed_ = false;
    force_safe(SafeReason::NotArmed);
}

void OutputBase::force_safe(SafeReason why) {
    const auto prev = static_cast<SafeReason>(reason_.exchange(static_cast<std::uint8_t>(why)));
    // Ghi an toàn NGAY (không chờ chu kỳ kế tiếp của thread riêng). Thread riêng có thể đang kẹt khi giữ khóa:
    // chỉ chờ ~5 ms; không lấy được thì cờ đã đặt – thread riêng sẽ tự ghi an toàn ở lần chạy kế tiếp.
    // Thử không chờ + nghỉ 1 ms (thay vì try_lock có timeout): hành vi như nhau trên mọi OS, và
    // ThreadSanitizer theo dõi được (TSan của GCC 13 không chặn pthread_mutex_clocklock của try_lock_for).
    for (int attempt = 0; attempt < kIoLockTries; ++attempt) {
        if (io_mtx_.try_lock(osal::kNoWait)) {
            write_safe();
            io_mtx_.unlock();
            break;
        }
        osal::sleep_ms(1);
    }
    if (prev != why) {
        safe_events_.fetch_add(1);
        if (why != SafeReason::NotArmed && why != SafeReason::Shutdown) {
            LOGW(kTag, "'%s' forced safe: %s", name_, to_string(why));
        }
        publish(evt::kOutputSafe, why);
    }
}

bool OutputBase::accept_command(std::uint64_t t) {
    if (!armed_.load() || reason() == SafeReason::Stalled || reason() == SafeReason::Shutdown) {
        rejected_.fetch_add(1);
        return false;
    }
    last_command_ms_ = ms32(t);
    commands_.fetch_add(1);
    // Lệnh mới đưa output ra khỏi "chưa có lệnh" / "lệnh quá hạn".
    if (set_reason(SafeReason::NoCommand, SafeReason::None) || set_reason(SafeReason::CommandTimeout, SafeReason::None)) {
        publish(evt::kOutputResumed, SafeReason::None);
    }
    return true;
}

void OutputBase::write_safe_now() {
    osal::LockGuard g(io_mtx_);
    write_safe();
}

void OutputBase::step(std::uint64_t t) {
    last_update_ms_ = ms32(t);   // nhịp tim cho supervisor
    if (cfg_.command_timeout_ms != 0 && output_allowed() && elapsed(t, last_command_ms_.load()) > cfg_.command_timeout_ms) {
        force_safe(SafeReason::CommandTimeout);   // phát hiện sớm ngay trên thread riêng
    }
    osal::LockGuard g(io_mtx_);
    // Kiểm tra quyền xuất và ghi phần cứng trong CÙNG vùng khóa: force_safe() không thể chen giữa.
    if (!output_allowed() || !update_with_latest(t)) {
        write_safe();   // chưa được xuất / chưa có lệnh: giữ an toàn (đồng thời làm mới phần cứng nếu cần)
        return;
    }
    updates_.fetch_add(1);
}

void OutputBase::supervise(std::uint64_t t) {
    if (!armed_.load()) return;
    // Deadman: cả chế độ trực tiếp (không có thread riêng để tự kiểm tra) lẫn thread riêng.
    if (cfg_.command_timeout_ms != 0 && reason() == SafeReason::None &&
        elapsed(t, last_command_ms_.load()) > cfg_.command_timeout_ms) {
        force_safe(SafeReason::CommandTimeout);
    }
    // Thread riêng của ACTUATOR không chạy quá hạn: kẹt (vòng lặp vô hạn, chờ phần cứng mãi...) -> an toàn + chốt.
    // Indicator không bị chốt: LED kẹt không nguy hiểm, và tắt nó có thể che mất tín hiệu cảnh báo.
    if (cfg_.cls == OutputClass::Actuator && threaded() && running_.load() && reason() != SafeReason::Stalled &&
        elapsed(t, last_update_ms_.load()) > stall_timeout()) {
        force_safe(SafeReason::Stalled);
    }
}

OutputStatus OutputBase::status() const {
    OutputStatus s;
    s.name        = name_;
    s.cls         = cfg_.cls;
    s.threaded    = threaded();
    s.armed       = armed_.load();
    s.reason      = reason();
    s.period_ms   = cfg_.update_period_ms;
    s.commands    = commands_.load();
    s.rejected    = rejected_.load();
    s.updates     = updates_.load();
    s.overruns    = overruns_.load();
    s.safe_events = safe_events_.load();
    return s;
}

core::Status OutputBase::start(const osal::ThreadConfig& thread, msg::EventBus* bus) {
    bus_ = bus;
    if (!threaded()) return core::Status::Ok;
    if (running_.load()) return core::Status::Busy;
    last_update_ms_ = ms32(now());
    schedule_.reset();
    running_              = true;
    const core::Status st = thread_.start(thread, &osal::Thread::invoke<OutputBase, &OutputBase::run>, this);
    if (!core::is_ok(st)) running_ = false;
    return st;
}

void OutputBase::stop() {
    armed_ = false;
    force_safe(SafeReason::Shutdown);
    running_ = false;
    thread_.join();
    write_safe_now();   // lần cuối, khi chắc chắn không còn thread nào ghi phần cứng
}

void OutputBase::run() {
    while (running_.load()) {
        const std::uint64_t t = now();
        if (schedule_.due(t)) {
            step(t);
            if (schedule_.advance(now(), cfg_.update_period_ms)) overruns_.fetch_add(1);
        }
        std::uint32_t wait = schedule_.wait(now(), cfg_.update_period_ms);
        while (wait > 0 && running_.load()) {
            const std::uint32_t s = wait < kPollMs ? wait : kPollMs;
            osal::sleep_ms(s);
            wait -= s;
        }
    }
}

// ------------------------------------------------------------ OutputHub
core::Status OutputHub::add(OutputBase* output, const osal::ThreadConfig& thread) {
    if (output == nullptr) return core::Status::InvalidArg;
    if (running_.load()) return core::Status::Busy;
    if (count_ >= slots_.size()) return core::Status::NoMemory;
    slots_[count_++] = Slot{output, thread};
    return core::Status::Ok;
}

void OutputHub::write_safe_all() {
    for (std::size_t i = 0; i < count_; ++i) slots_[i].output->write_safe_now();
}

void OutputHub::disarm_all() {
    for (std::size_t i = 0; i < count_; ++i) {
        if (slots_[i].output->output_class() == OutputClass::Actuator) slots_[i].output->disarm();
    }
}

core::Status OutputHub::start(msg::EventBus* bus, const osal::ThreadConfig& supervisor, std::uint32_t supervise_period_ms) {
    if (count_ == 0) return core::Status::Ok;
    period_ms_ = supervise_period_ms > 0 ? supervise_period_ms : 5;
    bool need_supervisor = false;
    for (std::size_t i = 0; i < count_; ++i) {
        OutputBase* o = slots_[i].output;
        need_supervisor = need_supervisor || o->needs_supervision();
        if (o->output_class() == OutputClass::Actuator && o->threaded() &&
            supervisor.priority.level() <= slots_[i].thread.priority.level()) {
            LOGW(kTag, "supervisor priority %u <= '%s' thread priority %u: a stalled actuator may starve it",
                 static_cast<unsigned>(supervisor.priority.level()), o->name(),
                 static_cast<unsigned>(slots_[i].thread.priority.level()));
        }
        const core::Status st = o->start(slots_[i].thread, bus);
        if (!core::is_ok(st)) {
            LOGE(kTag, "output '%s' failed to start: %s", o->name(), core::to_string(st));
            stop();
            return st;
        }
    }
    if (!need_supervisor) return core::Status::Ok;   // chỉ indicator: không cần thread giám sát
    running_              = true;
    const core::Status st = thread_.start(supervisor, &osal::Thread::invoke<OutputHub, &OutputHub::run>, this);
    if (!core::is_ok(st)) {
        running_ = false;
        stop();
    }
    return st;
}

void OutputHub::stop() {
    running_ = false;
    thread_.join();
    for (std::size_t i = count_; i-- > 0;) slots_[i].output->stop();   // mọi output về an toàn
}

void OutputHub::supervise_all(std::uint64_t now) {
    for (std::size_t i = 0; i < count_; ++i) slots_[i].output->supervise(now);
}

void OutputHub::run() {
    while (running_.load()) {
        supervise_all(osal::now_ms());
        osal::sleep_ms(period_ms_);
    }
}

}  // namespace svc
