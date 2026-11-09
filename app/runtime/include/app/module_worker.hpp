#pragma once
// Thread riêng (tùy chọn) của một module: cho nghiệp vụ chậm/chặn (ghi Flash, tính toán nặng, I/O đồng bộ)
// hoặc cần priority / chu kỳ khác với thread ứng dụng chung – mà không làm đứng các module khác.
//
//   class DataLogger : public app::IModule, private app::IWorkerHandler {
//       app::ModuleWorkerBase* worker() override { return &worker_; }
//       void on_event(const msg::Event& e) override {           // thread ứng dụng
//           if (e.id == events::kTemperatureBatch) worker_.post(e);   // Buffer: chỉ tăng tham chiếu
//       }
//       void on_job(const msg::Event& job) override { ... }       // thread worker: được phép chậm
//       ...dữ liệu do worker sở hữu...
//       app::ModuleWorker<8> worker_{*this, {"logger", osal::Priority::Low, 2048, nullptr}};   // khai báo CUỐI
//   };
// Khai báo worker_ là thành viên CUỐI: nó bị hủy đầu tiên -> thread dừng trước khi dữ liệu nó dùng bị hủy.
//
// ModuleManager khởi động worker sau on_start() của mọi module và dừng nó TRƯỚC on_stop(),
// nên on_init/on_start/on_stop luôn chạy khi worker chưa/không còn chạy.
// Hàng đợi và stack đều tĩnh (stack lấy từ pool lúc khởi động) – không cấp phát sau khởi động.
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "core/status.hpp"
#include "messaging/event.hpp"
#include "osal/queue.hpp"
#include "osal/thread.hpp"

namespace app {

// Phần chạy trên thread worker – module hiện thực những hàm cần dùng.
class IWorkerHandler {
public:
    virtual ~IWorkerHandler() = default;
    // Một việc do module gửi bằng ModuleWorker::post() (thường từ on_event/on_tick).
    virtual void on_job(const msg::Event& /*job*/) {}
    // Tác vụ định kỳ trên thread worker: 0 = không tick. Đọc lại mỗi vòng -> đổi được lúc chạy
    // (gọi từ thread worker: giá trị phải đọc được an toàn, vd. std::atomic).
    virtual std::uint32_t worker_period_ms() const { return 0; }
    // Chu kỳ KHÔNG trôi (lịch tính từ mốc trước + period); trễ quá một chu kỳ thì bỏ các lần lỡ.
    virtual void on_worker_tick(std::uint64_t /*now_ms*/) {}
};

class ModuleWorkerBase : core::NonCopyable {
public:
    virtual ~ModuleWorkerBase() { stop(); }

    // Gửi việc cho worker (an toàn từ mọi thread). false nếu hàng đợi đầy sau timeout -> tính vào dropped().
    bool post(const msg::Event& job, std::uint32_t timeout_ms = osal::kNoWait);

    // Do ModuleManager gọi. cfg.name == nullptr -> dùng cấu hình mặc định của module.
    core::Status start(const osal::ThreadConfig* override_cfg = nullptr);
    void stop();

    bool running() const { return running_.load(); }
    const osal::ThreadConfig& config() const { return cfg_; }
    std::uint32_t processed() const { return processed_.load(); }
    std::uint32_t dropped() const { return dropped_.load(); }
    std::uint32_t ticks() const { return ticks_.load(); }
    virtual std::size_t depth() const = 0;

protected:
    ModuleWorkerBase(IWorkerHandler& handler, const osal::ThreadConfig& cfg) : handler_(handler), cfg_(cfg) {}
    virtual bool push(const msg::Event& job, std::uint32_t timeout_ms) = 0;
    virtual bool pop(msg::Event& job, std::uint32_t timeout_ms)        = 0;

private:
    void run();

    IWorkerHandler& handler_;
    osal::ThreadConfig cfg_;
    osal::Thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint32_t> processed_{0};
    std::atomic<std::uint32_t> dropped_{0};
    std::atomic<std::uint32_t> ticks_{0};
};

// Depth: số việc chờ tối đa (mỗi việc là một msg::Event, payload lớn đi bằng Buffer – không copy).
template <std::size_t Depth>
class ModuleWorker final : public ModuleWorkerBase {
public:
    ModuleWorker(IWorkerHandler& handler, const osal::ThreadConfig& cfg) : ModuleWorkerBase(handler, cfg) {}
    ~ModuleWorker() override { stop(); }   // dừng thread TRƯỚC khi hàng đợi (của lớp này) bị hủy
    std::size_t depth() const override { return Depth; }

private:
    bool push(const msg::Event& job, std::uint32_t timeout_ms) override { return queue_.send(job, timeout_ms); }
    bool pop(msg::Event& job, std::uint32_t timeout_ms) override { return queue_.receive(job, timeout_ms); }

    osal::Queue<msg::Event, Depth> queue_;
};

}  // namespace app
