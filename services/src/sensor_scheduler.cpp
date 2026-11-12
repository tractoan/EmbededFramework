#include "services/sensor_scheduler.hpp"

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace svc {

namespace {
constexpr const char* kTag     = "sensors";
constexpr std::uint32_t kPollMs = 50;   // chờ tối đa mỗi vòng để kịp thấy yêu cầu dừng
}  // namespace

core::Status SensorScheduler::add(ISensorTask* task) {
    if (task == nullptr || task->period_ms() == 0) return core::Status::InvalidArg;
    if (running_.load()) return core::Status::Busy;
    // Task chặn chiếm trọn thread: không trộn với task khác (nó sẽ làm trễ cả nhóm).
    const bool is_blocking = task->mode() == SamplingMode::Blocking;
    if (count_ > 0 && (is_blocking || slots_[0].task->mode() == SamplingMode::Blocking)) {
        LOGE(kTag, "'%s': a blocking sensor task needs its own SensorScheduler", task->name());
        return core::Status::InvalidArg;
    }
    if (count_ >= slots_.size()) return core::Status::NoMemory;
    Slot& s            = slots_[count_++];
    s.task             = task;
    s.health.name      = task->name();
    s.health.period_ms = task->period_ms();
    s.health.mode      = task->mode();
    return core::Status::Ok;
}

std::uint64_t SensorScheduler::now() const { return cfg_.clock != nullptr ? cfg_.clock() : osal::now_ms(); }
SensorClock SensorScheduler::clock() const { return cfg_.clock != nullptr ? cfg_.clock : &osal::now_ms; }

std::uint32_t SensorScheduler::run_blocking_once() {
    const std::uint64_t start  = now();
    const core::Status st      = slots_[0].task->sample(clock());   // tự chờ dữ liệu, tối đa timeout của task
    const std::uint64_t after  = now();
    const std::uint32_t exec   = after > start ? static_cast<std::uint32_t>(after - start) : 0u;
    const std::uint32_t period = slots_[0].task->period_ms();
    record(0, st, after, exec, false);
    // Lỗi trả về NGAY (UART đóng, bus lỗi...) thay vì chờ: nghỉ phần còn lại của timeout, nếu không thread
    // priority cao sẽ quay liên tục và chiếm CPU. Có dữ liệu thì đọc tiếp ngay.
    return (!core::is_ok(st) && exec < period) ? period - exec : 0u;
}

std::uint32_t SensorScheduler::run_due() {
    if (count_ == 0) return kPollMs;
    if (blocking()) return run_blocking_once();
    for (std::size_t i = 0; i < count_; ++i) {
        Slot& s                    = slots_[i];
        const std::uint32_t period = s.task->period_ms();
        const std::uint64_t start  = now();
        if (!s.scheduled) {   // lần đầu: lấy mẫu ngay
            s.next_due  = start;
            s.scheduled = true;
        }
        if (start < s.next_due) continue;
        const core::Status st     = s.task->sample(clock());
        const std::uint64_t after = now();
        const std::uint32_t exec  = after > start ? static_cast<std::uint32_t>(after - start) : 0u;
        // Không trôi: mốc kế tiếp tính từ mốc trước. Đã trễ hơn một chu kỳ trọn vẹn (mốc đó cũng đã qua)
        // -> đặt lại lịch từ bây giờ, không chạy dồn.
        const bool overrun = after >= s.next_due + period;
        s.next_due         = overrun ? after + period : s.next_due + period;
        record(i, st, start, exec, overrun);
    }
    const std::uint64_t t = now();
    std::uint32_t wait    = kPollMs;
    for (std::size_t i = 0; i < count_; ++i) {
        const std::uint32_t until = slots_[i].next_due > t ? static_cast<std::uint32_t>(slots_[i].next_due - t) : 0u;
        if (until < wait) wait = until;
    }
    return wait;
}

void SensorScheduler::record(std::size_t i, core::Status st, std::uint64_t now_ms, std::uint32_t exec_ms,
                             bool overrun) {
    Slot& s          = slots_[i];
    bool fault_now   = false;
    bool recover_now = false;
    {
        osal::LockGuard g(health_mtx_);
        SensorHealth& h = s.health;
        if (exec_ms > h.max_exec_ms) h.max_exec_ms = exec_ms;
        if (overrun) ++h.overruns;
        if (core::is_ok(st)) {
            ++h.samples_ok;
            h.consecutive = 0;
            h.last_ok_ms  = now_ms;
            if (h.faulted) {
                h.faulted   = false;
                recover_now = true;
            }
        } else {
            ++h.errors;
            ++h.consecutive;
            h.last_error = st;
            if (!h.faulted && h.consecutive >= cfg_.fault_threshold) {
                h.faulted = true;
                fault_now = true;
            }
        }
    }
    // Phát event ngoài khóa: listener (vd. ModuleManager) chỉ đẩy vào hàng đợi của nó.
    if (fault_now) {
        LOGW(kTag, "'%s' faulted after %lu consecutive errors (%s)", s.task->name(),
             static_cast<unsigned long>(cfg_.fault_threshold), core::to_string(st));
        if (bus_ != nullptr) bus_->publish(msg::Event::make(evt::kSensorFault, SensorStatusEvent{s.task->name(), st}));
    } else if (recover_now) {
        LOGI(kTag, "'%s' recovered", s.task->name());
        if (bus_ != nullptr) {
            bus_->publish(msg::Event::make(evt::kSensorRecovered, SensorStatusEvent{s.task->name(), core::Status::Ok}));
        }
    }
}

bool SensorScheduler::health(std::size_t index, SensorHealth& out) const {
    if (index >= count_) return false;
    osal::LockGuard g(health_mtx_);
    out = slots_[index].health;
    return true;
}

core::Status SensorScheduler::start(const osal::ThreadConfig& thread, msg::EventBus* bus) {
    if (running_.load()) return core::Status::Busy;
    if (count_ == 0) return core::Status::InvalidArg;
    bus_     = bus;
    running_ = true;
    const core::Status st = thread_.start(thread, &osal::Thread::invoke<SensorScheduler, &SensorScheduler::run>, this);
    if (!core::is_ok(st)) running_ = false;
    return st;
}

void SensorScheduler::stop() {
    running_ = false;
    thread_.join();   // an toàn khi chưa từng start
}

void SensorScheduler::run() {
    while (running_.load()) {
        std::uint32_t wait = run_due();
        if (wait == 0) {
            osal::yield();
            continue;
        }
        while (wait > 0 && running_.load()) {   // ngủ ĐỦ, chia lát để kịp thấy yêu cầu dừng
            const std::uint32_t step = wait < kPollMs ? wait : kPollMs;
            osal::sleep_ms(step);
            wait -= step;
        }
    }
}

// ------------------------------------------------------------- SensorHub
core::Status SensorHub::add(SensorScheduler* group, const osal::ThreadConfig& thread) {
    if (group == nullptr) return core::Status::InvalidArg;
    if (count_ >= groups_.size()) return core::Status::NoMemory;
    groups_[count_++] = Group{group, thread};
    return core::Status::Ok;
}

core::Status SensorHub::start(msg::EventBus* bus) {
    for (std::size_t i = 0; i < count_; ++i) {
        const core::Status st = groups_[i].group->start(groups_[i].thread, bus);
        if (!core::is_ok(st)) {
            LOGE(kTag, "sensor group '%s' failed to start: %s", groups_[i].thread.name, core::to_string(st));
            stop();
            return st;
        }
    }
    return core::Status::Ok;
}

void SensorHub::stop() {
    for (std::size_t i = count_; i-- > 0;) groups_[i].group->stop();
}

}  // namespace svc
