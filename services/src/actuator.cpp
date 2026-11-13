#include "services/actuator.hpp"

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace svc {

namespace {
constexpr const char* kTag          = "actuator";
constexpr std::uint32_t kPollMs     = 50;   // ngủ tối đa mỗi lát để kịp thấy yêu cầu dừng
constexpr int kIoLockTries          = 5;    // force_safe chờ khóa phần cứng tối đa ~5 ms (5 lần x 1 ms)
constexpr std::uint32_t kMinStallMs = 20;

std::uint32_t ms32(std::uint64_t t) { return static_cast<std::uint32_t>(t); }
// Hiệu thời gian 32 bit, đúng cả khi bộ đếm quay vòng (~49 ngày).
std::uint32_t elapsed(std::uint64_t now, std::uint32_t since) { return ms32(now) - since; }
}  // namespace

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

ActuatorBase::ActuatorBase(const char* name, const Config& cfg) : name_(name), cfg_(cfg) {}

ActuatorBase::~ActuatorBase() {
    running_ = false;
    thread_.join();
}

std::uint64_t ActuatorBase::now() const { return cfg_.clock != nullptr ? cfg_.clock() : osal::now_ms(); }

std::uint32_t ActuatorBase::stall_timeout() const {
    if (cfg_.stall_timeout_ms != 0) return cfg_.stall_timeout_ms;
    const std::uint32_t t = cfg_.update_period_ms * 4u;
    return t > kMinStallMs ? t : kMinStallMs;
}

bool ActuatorBase::set_reason(SafeReason from, SafeReason to) {
    auto expected = static_cast<std::uint8_t>(from);
    return reason_.compare_exchange_strong(expected, static_cast<std::uint8_t>(to));
}

void ActuatorBase::publish(msg::EventId id, SafeReason r) {
    if (bus_ != nullptr) bus_->publish(msg::Event::make(id, ActuatorEvent{name_, r}));
}

void ActuatorBase::arm() {
    last_command_ms_ = ms32(now());   // deadman tính từ lúc arm
    reason_          = static_cast<std::uint8_t>(SafeReason::NoCommand);
    armed_           = true;
    LOGI(kTag, "'%s' armed", name_);
}

void ActuatorBase::disarm() {
    armed_ = false;
    force_safe(SafeReason::NotArmed);
}

void ActuatorBase::force_safe(SafeReason why) {
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
        publish(evt::kActuatorSafe, why);
    }
}

bool ActuatorBase::accept_command(std::uint64_t t) {
    if (!armed_.load() || reason() == SafeReason::Stalled || reason() == SafeReason::Shutdown) {
        rejected_.fetch_add(1);
        return false;
    }
    last_command_ms_ = ms32(t);
    commands_.fetch_add(1);
    // Lệnh mới đưa actuator ra khỏi "chưa có lệnh" / "lệnh quá hạn".
    if (set_reason(SafeReason::NoCommand, SafeReason::None) || set_reason(SafeReason::CommandTimeout, SafeReason::None)) {
        publish(evt::kActuatorResumed, SafeReason::None);
    }
    return true;
}

void ActuatorBase::write_safe_now() {
    osal::LockGuard g(io_mtx_);
    write_safe();
}

void ActuatorBase::step(std::uint64_t t) {
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

void ActuatorBase::supervise(std::uint64_t t) {
    if (!armed_.load()) return;
    // Deadman: cả chế độ trực tiếp (không có thread riêng để tự kiểm tra) lẫn thread riêng.
    if (cfg_.command_timeout_ms != 0 && reason() == SafeReason::None &&
        elapsed(t, last_command_ms_.load()) > cfg_.command_timeout_ms) {
        force_safe(SafeReason::CommandTimeout);
    }
    // Thread riêng không chạy quá hạn: kẹt (vòng lặp vô hạn, chờ phần cứng mãi...) -> an toàn + chốt.
    if (threaded() && running_.load() && reason() != SafeReason::Stalled &&
        elapsed(t, last_update_ms_.load()) > stall_timeout()) {
        force_safe(SafeReason::Stalled);
    }
}

ActuatorStatus ActuatorBase::status() const {
    ActuatorStatus s;
    s.name        = name_;
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

core::Status ActuatorBase::start(const osal::ThreadConfig& thread, msg::EventBus* bus) {
    bus_ = bus;
    if (!threaded()) return core::Status::Ok;
    if (running_.load()) return core::Status::Busy;
    last_update_ms_ = ms32(now());
    schedule_.reset();
    running_              = true;
    const core::Status st = thread_.start(thread, &osal::Thread::invoke<ActuatorBase, &ActuatorBase::run>, this);
    if (!core::is_ok(st)) running_ = false;
    return st;
}

void ActuatorBase::stop() {
    armed_ = false;
    force_safe(SafeReason::Shutdown);
    running_ = false;
    thread_.join();
    write_safe_now();   // lần cuối, khi chắc chắn không còn thread nào ghi phần cứng
}

void ActuatorBase::run() {
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

// ------------------------------------------------------------ ActuatorHub
core::Status ActuatorHub::add(ActuatorBase* actuator, const osal::ThreadConfig& thread) {
    if (actuator == nullptr) return core::Status::InvalidArg;
    if (running_.load()) return core::Status::Busy;
    if (count_ >= slots_.size()) return core::Status::NoMemory;
    slots_[count_++] = Slot{actuator, thread};
    return core::Status::Ok;
}

void ActuatorHub::write_safe_all() {
    for (std::size_t i = 0; i < count_; ++i) slots_[i].actuator->write_safe_now();
}

void ActuatorHub::disarm_all() {
    for (std::size_t i = 0; i < count_; ++i) slots_[i].actuator->disarm();
}

core::Status ActuatorHub::start(msg::EventBus* bus, const osal::ThreadConfig& supervisor, std::uint32_t supervise_period_ms) {
    if (count_ == 0) return core::Status::Ok;
    period_ms_ = supervise_period_ms > 0 ? supervise_period_ms : 5;
    for (std::size_t i = 0; i < count_; ++i) {
        ActuatorBase* a = slots_[i].actuator;
        if (a->threaded() && supervisor.priority.level() <= slots_[i].thread.priority.level()) {
            LOGW(kTag, "supervisor priority %u <= '%s' thread priority %u: a stalled actuator may starve it",
                 static_cast<unsigned>(supervisor.priority.level()), a->name(),
                 static_cast<unsigned>(slots_[i].thread.priority.level()));
        }
        const core::Status st = a->start(slots_[i].thread, bus);
        if (!core::is_ok(st)) {
            LOGE(kTag, "actuator '%s' failed to start: %s", a->name(), core::to_string(st));
            stop();
            return st;
        }
    }
    running_              = true;
    const core::Status st = thread_.start(supervisor, &osal::Thread::invoke<ActuatorHub, &ActuatorHub::run>, this);
    if (!core::is_ok(st)) {
        running_ = false;
        stop();
    }
    return st;
}

void ActuatorHub::stop() {
    running_ = false;
    thread_.join();
    for (std::size_t i = count_; i-- > 0;) slots_[i].actuator->stop();   // mọi actuator về an toàn
}

void ActuatorHub::supervise_all(std::uint64_t now) {
    for (std::size_t i = 0; i < count_; ++i) slots_[i].actuator->supervise(now);
}

void ActuatorHub::run() {
    while (running_.load()) {
        supervise_all(osal::now_ms());
        osal::sleep_ms(period_ms_);
    }
}

}  // namespace svc
