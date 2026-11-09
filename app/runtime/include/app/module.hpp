#pragma once
#include <cstdint>

#include "app/module_worker.hpp"
#include "app/system_context.hpp"
#include "app/system_events.hpp"
#include "core/status.hpp"
#include "messaging/event.hpp"

namespace app {

// Hợp đồng của một module nghiệp vụ (plug-in). Callback chạy trên thread nào:
//   on_init / on_start / on_stop : thread ứng dụng, khi worker của module CHƯA hoặc KHÔNG CÒN chạy
//   on_event / on_tick           : thread ứng dụng (chung cho mọi module, tuần tự) => không cần khóa
//   on_job / on_worker_tick      : thread worker riêng của module (nếu có, xem worker())
//   on_command (ICommandHandler) : thread của kênh nhận lệnh
// Không có worker: mọi trạng thái của module chỉ do thread ứng dụng chạm tới -> không cần khóa.
// Có worker: trạng thái do worker sở hữu chỉ đọc/ghi trên worker; trao đổi giữa hai phía bằng
// ModuleWorker::post() (ứng dụng -> worker) và EventBus::publish() (worker -> ứng dụng/module khác),
// hoặc biến std::atomic cho cấu hình đơn giản.
class IModule {
public:
    virtual ~IModule() = default;
    virtual const char* name() const = 0;

    // Đăng ký tham số / lệnh CLI, lấy tham chiếu dịch vụ. Chưa được phát event.
    virtual core::Status on_init(SystemContext& ctx) = 0;
    // Tham số đã nạp từ NV – đọc cấu hình, khởi động state machine.
    virtual core::Status on_start() { return core::Status::Ok; }
    virtual void on_stop() {}

    virtual void on_event(const msg::Event& /*e*/) {}

    // Tác vụ định kỳ: trả 0 nếu không cần tick.
    virtual std::uint32_t tick_period_ms() const { return 0; }
    virtual void on_tick(std::uint64_t /*now_ms*/) {}

    // Thread riêng của module (tùy chọn): trả về ModuleWorker do module sở hữu, nullptr = không có.
    virtual ModuleWorkerBase* worker() { return nullptr; }
};

}  // namespace app
