// Sản phẩm "thermo_lite": dân dụng – lấy mẫu chậm, cảnh báo tự phục hồi (không latch),
// chỉ có CLI trên console (không link Modbus -> không tốn Flash cho giao thức đó).
// Dùng lại 100% module của thư viện, chỉ khác cấu hình.
#include "alarm_controller/alarm_controller.hpp"
#include "app/product.hpp"
#include "comm/cli_protocol.hpp"
#include "messaging/buffer_allocators.hpp"
#include "temperature_monitor/temperature_monitor.hpp"

namespace product {

const char* name() { return "thermo_lite"; }

core::Status setup(app::ProductSetup& s) {
    app::TemperatureMonitor::Config tc;
    tc.sample_period_ms = 2000;
    tc.filter_window    = 2;
    tc.max_batch_size   = 52;   // vừa block 128 B của pool

    app::AlarmController::Config ac;
    ac.warn_deci = 450;
    ac.high_deci = 550;
    ac.latch     = false;

    // MCU nhỏ: một pool tĩnh 4 x 128 B, không heap. Lô mặc định tắt (batch_size = 0);
    // bật bằng "param set temp.batch_n <n>" với n <= 52 mẫu.
    static msg::StaticBlockPool<128, 4> pool_blocks;
    static msg::PoolAllocator pool({&pool_blocks});
    msg::Buffer::set_default_allocator(&pool);

    // MCU nhỏ: stack gọn hơn (tổng 7 KB, vừa EP_CFG_STACK_POOL_BYTES = 8 KB trong product_config.hpp).
    s.tasks.app  = osal::ThreadConfig{"app", osal::Priority::High, 3072, nullptr};
    s.tasks.leds = osal::ThreadConfig{"led", osal::Priority::Low, 1024, nullptr};

    static app::TemperatureMonitor temperature(tc);
    static app::AlarmController alarm(ac);
    static comm::CliProtocol console_cli;

    core::Status st = s.modules.add(&temperature);
    if (core::is_ok(st)) st = s.modules.add(&alarm);
    if (core::is_ok(st)) {
        st = s.links.add_channel("console", &s.console, {&console_cli}, comm::proto::kCli, osal::Priority::Normal, 3072);
    }
    return st;
}

}  // namespace product
