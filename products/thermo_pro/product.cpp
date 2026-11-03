// Sản phẩm "thermo_pro": giám sát nhiệt công nghiệp – lấy mẫu nhanh, cảnh báo có chốt (latch),
// kèm module thống kê riêng của sản phẩm.
#include "alarm_controller/alarm_controller.hpp"
#include "app/product.hpp"
#include "stats_reporter.hpp"
#include "temperature_monitor/temperature_monitor.hpp"

namespace product {

namespace {
app::TemperatureMonitor::Config temp_cfg() {
    app::TemperatureMonitor::Config c;
    c.sample_period_ms = 500;
    c.filter_window    = 4;
    c.fault_threshold  = 3;
    return c;
}

app::AlarmController::Config alarm_cfg() {
    app::AlarmController::Config c;
    c.warn_deci       = 600;
    c.high_deci       = 750;
    c.hysteresis_deci = 20;
    c.latch           = true;   // công nghiệp: người vận hành phải xác nhận
    return c;
}
}  // namespace

const char* name() { return "thermo_pro"; }

core::Status register_modules(app::ModuleManager& modules) {
    // Cấp phát tĩnh – không dùng heap.
    static app::TemperatureMonitor temperature(temp_cfg());
    static app::AlarmController alarm(alarm_cfg());
    static StatsReporter stats;

    core::Status st = modules.add(&temperature);
    if (core::is_ok(st)) st = modules.add(&alarm);
    if (core::is_ok(st)) st = modules.add(&stats);
    return st;
}

}  // namespace product
