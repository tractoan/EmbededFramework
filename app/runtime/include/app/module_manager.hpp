#pragma once
#include <array>
#include <atomic>

#include "app/module.hpp"
#include "osal/queue.hpp"
#include "osal/thread.hpp"

namespace app {

// Active Object cho toàn bộ tầng ứng dụng:
//  - Nhận MỌI event từ EventBus (từ bất kỳ thread/service nào) -> đưa vào queue riêng.
//  - Một thread duy nhất lấy event ra, phân phối tới các module, và gọi on_tick đúng chu kỳ.
// => Nghiệp vụ chạy tuần tự, xác định, không race condition.
class ModuleManager final : public svc::IEventListener, private core::NonCopyable {
public:
    core::Status add(IModule* module);
    core::Status init_all(SystemContext& ctx);
    core::Status start();
    void stop();

    void on_event(const svc::Event& e) override;   // gọi từ thread bất kỳ, không chặn

    // Một vòng xử lý (chờ tối đa max_wait_ms). Dùng cho super-loop bare-metal hoặc test.
    void process(std::uint32_t max_wait_ms);

    std::size_t count() const { return count_; }
    IModule* at(std::size_t i) const { return i < count_ ? slots_[i].module : nullptr; }
    std::uint32_t dropped_events() const { return dropped_.load(); }

private:
    void run();

    struct Slot {
        IModule* module         = nullptr;
        std::uint64_t next_tick = 0;
    };
    std::array<Slot, core::cfg::kMaxModules> slots_{};
    std::size_t count_ = 0;
    osal::Queue<svc::Event, core::cfg::kAppQueueDepth> queue_;
    osal::Thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint32_t> dropped_{0};
    SystemContext* ctx_ = nullptr;
};

}  // namespace app
