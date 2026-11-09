#include "app/module_worker.hpp"

#include "osal/time.hpp"

namespace app {

namespace {
// Chờ tối đa chừng này mỗi vòng để kịp thấy yêu cầu dừng (giống ModuleManager).
constexpr std::uint32_t kStopPollMs = 50;
}  // namespace

bool ModuleWorkerBase::post(const msg::Event& job, std::uint32_t timeout_ms) {
    if (push(job, timeout_ms)) return true;
    dropped_.fetch_add(1);
    return false;
}

core::Status ModuleWorkerBase::start(const osal::ThreadConfig* override_cfg) {
    if (running_.load()) return core::Status::Busy;
    if (override_cfg != nullptr && override_cfg->name != nullptr) cfg_ = *override_cfg;
    running_ = true;
    const core::Status st = thread_.start(cfg_, &osal::Thread::invoke<ModuleWorkerBase, &ModuleWorkerBase::run>, this);
    if (!core::is_ok(st)) running_ = false;
    return st;
}

void ModuleWorkerBase::stop() {
    running_ = false;
    thread_.join();   // an toàn khi thread chưa từng chạy
}

void ModuleWorkerBase::run() {
    std::uint64_t now         = osal::now_ms();
    std::uint32_t last_period = handler_.worker_period_ms();
    std::uint64_t next_tick   = now + last_period;

    while (running_.load()) {
        std::uint32_t period = handler_.worker_period_ms();
        if (period != last_period) {   // bật/tắt/đổi chu kỳ lúc chạy: tính lại mốc từ bây giờ
            last_period = period;
            next_tick   = now + period;
        }
        std::uint32_t wait = kStopPollMs;
        if (period != 0) {
            wait = next_tick <= now ? 0 : (next_tick - now < wait ? static_cast<std::uint32_t>(next_tick - now) : wait);
        }

        msg::Event job;
        if (pop(job, wait)) {
            handler_.on_job(job);
            processed_.fetch_add(1);
            job = msg::Event{};   // nhả Buffer ngay (không giữ tới vòng sau)
        }

        now = osal::now_ms();
        if (period != 0 && period == handler_.worker_period_ms() && now >= next_tick) {
            handler_.on_worker_tick(now);
            ticks_.fetch_add(1);
            next_tick += period;                       // không trôi: mốc kế tiếp tính từ mốc trước
            if (now >= next_tick) next_tick = now + period;   // trễ > 1 chu kỳ: bỏ các lần lỡ, không gọi dồn
        }
    }
}

}  // namespace app
