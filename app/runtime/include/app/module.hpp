#pragma once
#include <cstdint>

#include "app/system_context.hpp"
#include "app/system_events.hpp"
#include "core/status.hpp"
#include "messaging/event.hpp"

namespace app {

// Hợp đồng của một module nghiệp vụ (plug-in).
// Mọi callback được gọi trên CÙNG một thread ứng dụng => bên trong module không cần khóa.
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
};

}  // namespace app
