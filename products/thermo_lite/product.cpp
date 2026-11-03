// Sản phẩm "thermo_lite": dân dụng – lấy mẫu chậm, cảnh báo tự phục hồi (không latch).
// Dùng lại 100% module của thư viện, chỉ khác cấu hình.
#include "alarm_controller/alarm_controller.hpp"
#include "app/product.hpp"
#include "temperature_monitor/temperature_monitor.hpp"

namespace product {

const char* name() { return "thermo_lite"; }

core::Status register_modules(app::ModuleManager& modules) {
    app::TemperatureMonitor::Config tc;
    tc.sample_period_ms = 2000;
    tc.filter_window    = 2;

    app::AlarmController::Config ac;
    ac.warn_deci = 450;
    ac.high_deci = 550;
    ac.latch     = false;

    static app::TemperatureMonitor temperature(tc);
    static app::AlarmController alarm(ac);

    core::Status st = modules.add(&temperature);
    return core::is_ok(st) ? modules.add(&alarm) : st;
}

}  // namespace product
