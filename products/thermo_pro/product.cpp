// Sản phẩm "thermo_pro": giám sát nhiệt công nghiệp – lấy mẫu nhanh, cảnh báo có chốt (latch),
// module thống kê riêng, giao tiếp CLI / Modbus RTU / MAVLink v1 qua UART, TCP và UDP,
// đổi giao thức lúc chạy.
#include "alarm_controller/alarm_controller.hpp"
#include "app/product.hpp"
#include "comm/cli_protocol.hpp"
#include "mavlink/mavlink_protocol.hpp"
#include "mavlink_map.hpp"
#include "modbus/modbus_rtu.hpp"
#include "modbus_map.hpp"
#include "messaging/buffer_allocators.hpp"
#include "stats_reporter.hpp"
#include "temperature_monitor/temperature_monitor.hpp"

#if EP_HAS_NET
#include "comm/net_streams.hpp"
#endif

namespace product {

namespace {
app::TemperatureMonitor::Config temp_cfg() {
    app::TemperatureMonitor::Config c;
    c.sample_period_ms = 500;
    c.filter_window    = 4;
    c.fault_threshold  = 3;
    c.batch_size       = 20;    // lô 20 mẫu = 10 s dữ liệu -> StatsReporter
    c.max_batch_size   = 1000;  // vừa block 2048 B lớn nhất của pool
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

// Bộ nhớ cho payload lớn: chỉ pool tĩnh 3 lớp kích thước (O(1), không phân mảnh, không heap).
// Sức chứa mỗi block tính theo header 24 B trên host 64-bit (MCU 32-bit: 16 B).
void setup_buffers() {
    static msg::StaticBlockPool<64, 16> small;    // lô <= 20 mẫu
    static msg::StaticBlockPool<512, 8> medium;   // lô <= 244 mẫu
    static msg::StaticBlockPool<2048, 4> large;   // lô <= 1012 mẫu
    static msg::PoolAllocator pools({&small, &medium, &large});
    msg::Buffer::set_default_allocator(&pools);
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

#if EP_HAS_NET
// Kênh mạng (chỉ khi target có TCP/IP – EP_NET_PORT):
//   tcp : server cổng 5760 (cổng TCP chuẩn của MAVLink), mặc định MAVLink; đổi được sang Modbus/CLI
//   udp : gửi tới trạm mặt đất ở cổng 14550 (mặc định của QGroundControl/Mission Planner)
constexpr std::uint16_t kTcpPort       = 5760;
constexpr std::uint16_t kGcsUdpPort    = 14550;
constexpr const char* kGcsAddress      = "127.0.0.1";   // địa chỉ trạm mặt đất (cấu hình theo hệ thống)

core::Status setup_network_links(app::ProductSetup& s) {
    static comm::TcpServerStream tcp;
    if (core::is_ok(tcp.open(kTcpPort))) {
        static mavlink::MavlinkV1Protocol tcp_mavlink(mavlink_map());
        static modbus::ModbusRtuProtocol tcp_modbus(modbus_map());
        static comm::CliProtocol tcp_cli;
        const core::Status st = s.links.add_channel("tcp", &tcp, {&tcp_mavlink, &tcp_modbus, &tcp_cli},
                                                    comm::proto::kMavlink, osal::Priority::Normal, 4096);
        if (!core::is_ok(st)) return st;
    }
    static comm::UdpStream udp;
    net::Endpoint gcs{};
    if (core::is_ok(udp.open(0)) && net::Endpoint::parse(kGcsAddress, kGcsUdpPort, gcs)) {
        udp.set_remote(gcs);
        static mavlink::MavlinkV1Protocol udp_mavlink(mavlink_map());
        return s.links.add_channel("udp", &udp, {&udp_mavlink}, comm::proto::kMavlink, osal::Priority::Normal, 4096);
    }
    return core::Status::Ok;   // không có mạng lúc khởi động: chạy tiếp không có kênh mạng
}
#else
core::Status setup_network_links(app::ProductSetup&) { return core::Status::Ok; }
#endif

core::Status setup_links(app::ProductSetup& s) {
    // Mỗi instance giao thức gắn với một kênh (có trạng thái phiên riêng).
    // Thứ tự trong danh sách = thứ tự thử khi auto-detect (giao thức nhị phân, chặt chẽ hơn, đặt trước).
    static comm::CliProtocol console_cli;
    static modbus::ModbusRtuProtocol console_modbus(modbus_map());
    core::Status st = s.links.add_channel("console", &s.console, {&console_modbus, &console_cli}, comm::proto::kCli,
                                          osal::Priority{6}, 4096);
    if (!core::is_ok(st)) return st;

    // Cổng phụ (RS485 / radio telemetry trên board thật, UDP trên mô phỏng): mặc định Modbus,
    // đổi được sang MAVLink/CLI hoặc auto (tự nhận ra Modbus hay MAVLink từ khung đầu tiên).
    static comm::UartStream aux;
    aux.attach(s.board.uart(hal::UartId::Aux));
    if (aux.valid()) {
        static comm::CliProtocol aux_cli;
        static modbus::ModbusRtuProtocol aux_modbus(modbus_map());
        static mavlink::MavlinkV1Protocol aux_mavlink(mavlink_map());
        st = s.links.add_channel("aux", &aux, {&aux_modbus, &aux_mavlink, &aux_cli}, comm::proto::kModbusRtu,
                                 osal::Priority::Normal, 4096);
        if (!core::is_ok(st)) return st;
    }
    return setup_network_links(s);
}
}  // namespace

const char* name() { return "thermo_pro"; }

// Ưu tiên task (0 thấp .. 15 cao): nghiệp vụ cảnh báo cao nhất; Modbus (cổng phụ, master cần đáp
// nhanh) trên console người vận hành; LED thấp nhất. Stack lấy từ vùng stack tĩnh lúc khởi động.
void setup_tasks(app::TaskPlan& t) {
    t.app  = osal::ThreadConfig{"app", osal::Priority::High, 4096, nullptr};
    t.leds = osal::ThreadConfig{"led", osal::Priority::Low, 1024, nullptr};
}

core::Status setup(app::ProductSetup& s) {
    setup_buffers();
    setup_tasks(s.tasks);
    const core::Status st = setup_modules(s.modules);
    return core::is_ok(st) ? setup_links(s) : st;
}

}  // namespace product
