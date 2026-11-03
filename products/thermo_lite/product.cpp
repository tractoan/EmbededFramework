// Sản phẩm "thermo_lite": dân dụng – lấy mẫu chậm, cảnh báo tự phục hồi (không latch),
// chỉ có CLI trên console (không link Modbus -> không tốn Flash cho giao thức đó).
// Dùng lại 100% module của thư viện, chỉ khác cấu hình.
#include "alarm_controller/alarm_controller.hpp"
#include "app/product.hpp"
#include "comm/cli_protocol.hpp"
#include "temperature_monitor/temperature_monitor.hpp"

namespace product {

const char* name() { return "thermo_lite"; }

core::Status setup(app::ProductSetup& s) {
    app::TemperatureMonitor::Config tc;
    tc.sample_period_ms = 2000;
    tc.filter_window    = 2;

    app::AlarmController::Config ac;
    ac.warn_deci = 450;
    ac.high_deci = 550;
    ac.latch     = false;

    static app::TemperatureMonitor temperature(tc);
    static app::AlarmController alarm(ac);
    static comm::CliProtocol console_cli;

    core::Status st = s.modules.add(&temperature);
    if (core::is_ok(st)) st = s.modules.add(&alarm);
    if (core::is_ok(st)) st = s.links.add_channel("console", &s.console, {&console_cli}, comm::proto::kCli);
    return st;
}

}  // namespace product
