#include "app/module_manager.hpp"

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace app {

namespace {
constexpr const char* kTag = "modules";
}

core::Status ModuleManager::add(IModule* module, const osal::ThreadConfig* worker_task) {
    if (module == nullptr) return core::Status::InvalidArg;
    if (count_ >= slots_.size()) return core::Status::NoMemory;
    if (worker_task != nullptr && module->worker() == nullptr) return core::Status::InvalidArg;   // module không có worker
    Slot& s  = slots_[count_++];
    s.module = module;
    if (worker_task != nullptr) s.worker_task = *worker_task;
    return core::Status::Ok;
}

core::Status ModuleManager::init_all(SystemContext& ctx) {
    ctx_ = &ctx;
    for (std::size_t i = 0; i < count_; ++i) {
        const core::Status st = slots_[i].module->on_init(ctx);
        if (!core::is_ok(st)) {
            LOGE(kTag, "init '%s' failed: %s", slots_[i].module->name(), core::to_string(st));
            return st;
        }
        LOGD(kTag, "init '%s' ok", slots_[i].module->name());
    }
    return ctx.bus.subscribe(msg::evt::kAny, this);
}

core::Status ModuleManager::start(const osal::ThreadConfig& task) {
    const std::uint64_t now = osal::now_ms();
    for (std::size_t i = 0; i < count_; ++i) {
        const core::Status st = slots_[i].module->on_start();
        if (!core::is_ok(st)) {
            LOGE(kTag, "start '%s' failed: %s", slots_[i].module->name(), core::to_string(st));
            return st;
        }
        slots_[i].next_tick = now + slots_[i].module->tick_period_ms();
    }
    // Worker chạy trước thread ứng dụng: việc post từ on_event đầu tiên đã có người nhận.
    for (std::size_t i = 0; i < count_; ++i) {
        ModuleWorkerBase* w = slots_[i].module->worker();
        if (w == nullptr) continue;
        const core::Status st = w->start(&slots_[i].worker_task);
        if (!core::is_ok(st)) {
            LOGE(kTag, "worker of '%s' failed to start: %s", slots_[i].module->name(), core::to_string(st));
            stop_workers();
            return st;
        }
        LOGD(kTag, "worker '%s' of '%s' started", w->config().name, slots_[i].module->name());
    }
    running_ = true;
    LOGI(kTag, "%u module(s) running", static_cast<unsigned>(count_));
    const core::Status st = thread_.start(task, &osal::Thread::invoke<ModuleManager, &ModuleManager::run>, this);
    if (!core::is_ok(st)) {
        running_ = false;
        stop_workers();
    }
    return st;
}

void ModuleManager::stop() {
    if (ctx_ != nullptr) ctx_->bus.unsubscribe(msg::evt::kAny, this);
    running_ = false;
    thread_.join();   // dừng thread ứng dụng trước: không còn ai post việc mới cho worker
    stop_workers();   // rồi worker: on_stop() chạy khi module không còn thread nào khác
    for (std::size_t i = count_; i-- > 0;) slots_[i].module->on_stop();
}

void ModuleManager::stop_workers() {
    for (std::size_t i = count_; i-- > 0;) {
        if (ModuleWorkerBase* w = slots_[i].module->worker()) w->stop();
    }
}

void ModuleManager::on_event(const msg::Event& e) {
    if (!queue_.send(e, osal::kNoWait)) dropped_.fetch_add(1);
}

void ModuleManager::process(std::uint32_t max_wait_ms) {
    std::uint64_t now = osal::now_ms();
    std::uint32_t wait = max_wait_ms;
    for (std::size_t i = 0; i < count_; ++i) {
        if (slots_[i].module->tick_period_ms() == 0) continue;
        if (slots_[i].next_tick <= now) {
            wait = 0;
        } else if (slots_[i].next_tick - now < wait) {
            wait = static_cast<std::uint32_t>(slots_[i].next_tick - now);
        }
    }

    msg::Event e;
    if (queue_.receive(e, wait)) {
        for (std::size_t i = 0; i < count_; ++i) slots_[i].module->on_event(e);
    }

    now = osal::now_ms();
    for (std::size_t i = 0; i < count_; ++i) {
        const std::uint32_t period = slots_[i].module->tick_period_ms();
        if (period != 0 && now >= slots_[i].next_tick) {
            slots_[i].module->on_tick(now);
            slots_[i].next_tick = now + period;
        }
    }
}

void ModuleManager::run() {
    while (running_) process(50);
}

}  // namespace app
