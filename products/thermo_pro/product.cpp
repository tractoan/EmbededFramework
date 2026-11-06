// Sản phẩm "thermo_pro": giám sát nhiệt công nghiệp – lấy mẫu nhanh, cảnh báo có chốt (latch),
// module thống kê riêng, giao tiếp CLI + Modbus RTU trên 2 kênh, đổi giao thức lúc chạy.
#include "alarm_controller/alarm_controller.hpp"
#include "app/product.hpp"
#include "comm/cli_protocol.hpp"
#include "modbus/modbus_rtu.hpp"
#include "modbus_map.hpp"
#include "services/buffer_allocators.hpp"
#include "stats_reporter.hpp"
#include "temperature_monitor/temperature_monitor.hpp"

namespace product {

namespace {
app::TemperatureMonitor::Config temp_cfg() {
    app::TemperatureMonitor::Config c;
    c.sample_period_ms = 500;
    c.filter_window    = 4;
    c.fault_threshold  = 3;
    c.batch_size       = 20;    // lô 20 mẫu = 10 s dữ liệu -> StatsReporter
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

// Bộ nhớ cho payload lớn: pool tĩnh 3 lớp kích thước (O(1), không phân mảnh) cho trường hợp
// thường gặp; vượt quá -> heap có giới hạn 64 KB cho lô rất lớn (temp.batch_n tới 4096 mẫu).
void setup_buffers() {
    static svc::StaticBlockPool<64, 16> small;    // lô <= 20 mẫu
    static svc::StaticBlockPool<512, 8> medium;   // lô <= 244 mẫu
    static svc::StaticBlockPool<2048, 4> large;   // lô <= 1012 mẫu
    static svc::PoolAllocator pools({&small, &medium, &large});
    static svc::HeapAllocator heap(64 * 1024);
    static svc::ChainAllocator allocator(pools, heap);
    svc::Buffer::set_default_allocator(&allocator);
}

core::Status setup_modules(app::ModuleManager& modules) {
    // Cấp phát tĩnh – không dùng heap.
    static app::TemperatureMonitor temperature(temp_cfg());
    static app::AlarmController alarm(alarm_cfg());
    static StatsReporter stats;

    core::Status st = modules.add(&temperature);
    if (core::is_ok(st)) st = modules.add(&alarm);
    if (core::is_ok(st)) st = modules.add(&stats);
    return st;
}

core::Status setup_links(app::ProductSetup& s) {
    // Mỗi instance giao thức gắn với một kênh (có trạng thái phiên riêng).
    // Thứ tự trong danh sách = thứ tự thử khi auto-detect (giao thức nhị phân, chặt chẽ hơn, đặt trước).
    static comm::CliProtocol console_cli;
    static modbus::ModbusRtuProtocol console_modbus(modbus_map());
    core::Status st = s.links.add_channel("console", &s.console, {&console_modbus, &console_cli}, comm::proto::kCli);
    if (!core::is_ok(st)) return st;

    // Cổng phụ (RS485 trên board thật, UDP trên mô phỏng): mặc định Modbus, đổi được sang CLI/auto.
    static comm::UartStream aux;
    aux.attach(s.board.uart(hal::UartId::Aux));
    if (!aux.valid()) return core::Status::Ok;
    static comm::CliProtocol aux_cli;
    static modbus::ModbusRtuProtocol aux_modbus(modbus_map());
    return s.links.add_channel("aux", &aux, {&aux_modbus, &aux_cli}, comm::proto::kModbusRtu);
}
}  // namespace

const char* name() { return "thermo_pro"; }

core::Status setup(app::ProductSetup& s) {
    setup_buffers();
    const core::Status st = setup_modules(s.modules);
    return core::is_ok(st) ? setup_links(s) : st;
}

}  // namespace product
