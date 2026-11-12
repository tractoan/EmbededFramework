#pragma once
#include "app/module_manager.hpp"
#include "comm/link_manager.hpp"
#include "hal/board.hpp"
#include "services/sensor_scheduler.hpp"

namespace app {

// Priority & stack của các task do framework tạo. Sản phẩm sửa trong product::setup().
// stack_mem == nullptr -> stack lấy từ vùng stack tĩnh (EP_CFG_STACK_POOL_BYTES) lúc khởi động;
// hoặc trỏ tới buffer static của sản phẩm để đặt stack ở vùng RAM mong muốn.
// Task của kênh truyền thông cấu hình riêng qua LinkManager::add_channel(..., priority, stack_bytes).
struct TaskPlan {
    osal::ThreadConfig app{"app", osal::Priority::Normal, 4096, nullptr};    // ModuleManager (nghiệp vụ)
    osal::ThreadConfig leds{"led", osal::Priority::Low, 1024, nullptr};      // LedService
};

// Những gì một sản phẩm được phép "lắp ráp" khi khởi động.
struct ProductSetup {
    ModuleManager& modules;      // module nghiệp vụ
    comm::LinkManager& links;    // kênh truyền thông + giao thức cho phép trên từng kênh
    hal::IBoard& board;          // để lấy thêm cổng (vd. UartId::Aux)
    comm::IByteStream& console;  // cổng console đã sẵn sàng
    TaskPlan& tasks;             // priority/stack của task framework
    svc::SensorHub& sensors;     // nhóm lấy mẫu cảm biến (SensorScheduler) – System khởi động/dừng
};

}  // namespace app

// Mỗi sản phẩm (products/<name>/product.cpp) hiện thực 2 hàm này.
namespace product {
const char* name();
core::Status setup(app::ProductSetup& s);
}  // namespace product
