// Sản phẩm "hw_bringup": firmware chạy trên CPU để bring-up phần cứng.
//  - 12 cổng serial RS232/485: cổng kGcsPort nối phần mềm giao diện qua MAVLink, cổng kGnssPort nối
//    máy thu GNSS (không có dữ liệu trong 5 s đầu -> chuyển sang kiểm tra loopback), 10 cổng còn lại
//    kiểm tra loopback liên tục.
//  - 20 DI / 20 DO, 2 IMU giao tiếp SDLC/HDLC.
//  - Lên giao diện: trạng thái loopback, trạng thái input, dữ liệu IMU, dữ liệu GNSS, sự kiện.
//    Từ giao diện: đặt output, lệnh cấu hình gửi tới máy thu GNSS, tham số.
#include <cstdio>

#include "app/product.hpp"
#include "bringup_config.hpp"
#include "comm/cli_protocol.hpp"
#include "imu_bringup/imu_bringup.hpp"
#include "io_bringup/io_bringup.hpp"
#include "mavlink2/mavlink_protocol.hpp"
#include "mavlink_dialect.hpp"
#include "serial_bringup/serial_bringup.hpp"
#include "device/digital_io.hpp"
#include "services/hdlc_imu.hpp"
#include "messaging/buffer_allocators.hpp"
#include "services/logger.hpp"
#include "services/serial_port_bank.hpp"

#if EP_HAS_NET
#include "comm/net_streams.hpp"
#endif

namespace product {

namespace {
constexpr const char* kTag = "bringup";

// Ưu tiên task (0 thấp .. 15 cao): đọc IMU cao nhất (không để tràn FIFO HDLC), cổng GNSS (stream)
// và kênh giao diện trên nghiệp vụ; các cổng loopback và console thấp hơn; LED thấp nhất.
constexpr osal::Priority kImuPriority      = osal::Priority::High;      // 12
constexpr osal::Priority kGnssPortPriority = osal::Priority{10};
constexpr osal::Priority kGcsLinkPriority  = osal::Priority{9};
constexpr osal::Priority kAppPriority      = osal::Priority::Normal;    // 8
constexpr osal::Priority kLoopPriority     = osal::Priority{5};
constexpr osal::Priority kConsolePriority  = osal::Priority::Low;       // 4

// Thiết bị/dịch vụ của sản phẩm – cấp phát tĩnh.
svc::SerialPortBank g_ports;
device::GpioInputBank g_inputs;
device::GpioOutputBank g_outputs;
svc::HdlcImu g_imu[hw::kImuCount];

core::Status setup_serial(hal::IBoard& board) {
    static char names[hw::kSerialCount][8];
    if (board.serial_count() < hw::kSerialCount) {
        LOGW(kTag, "board has %u serial ports, %u expected (missing ports report OPEN-ERR)",
             static_cast<unsigned>(board.serial_count()), static_cast<unsigned>(hw::kSerialCount));
    }
    for (std::size_t i = 0; i < hw::kSerialCount; ++i) {
        std::snprintf(names[i], sizeof(names[i]), "ser%u", static_cast<unsigned>(i));
        svc::SerialPortBank::PortConfig pc;
        pc.uart               = board.serial(i);
        pc.uart_cfg.baudrate  = hw::kLoopbackBaudrate;
        pc.peer               = hw::kLoopbackPeer[i];
        pc.name               = names[i];
        pc.mode = static_cast<int>(i) == hw::kGcsPort    ? svc::SerialMode::External
                  : static_cast<int>(i) == hw::kGnssPort ? svc::SerialMode::Stream
                                                         : svc::SerialMode::Loopback;
        pc.priority    = static_cast<int>(i) == hw::kGnssPort ? kGnssPortPriority : kLoopPriority;
        pc.stack_bytes = 2048;   // stack_mem = nullptr -> vùng stack tĩnh lúc khởi động
        const core::Status st = g_ports.add(pc);
        if (!core::is_ok(st)) return st;
    }
    return core::Status::Ok;
}

void setup_io(hal::IBoard& board) {
    for (std::size_t i = 0; i < hw::kInputCount && i < board.digital_input_count(); ++i) g_inputs.add(board.digital_input(i));
    for (std::size_t i = 0; i < hw::kOutputCount && i < board.digital_output_count(); ++i) g_outputs.add(board.digital_output(i));
    g_inputs.set_invert_mask(hw::kInputInvertMask);
    if (g_inputs.count() < hw::kInputCount || g_outputs.count() < hw::kOutputCount) {
        LOGW(kTag, "board has %u DI / %u DO, %u / %u expected", static_cast<unsigned>(g_inputs.count()),
             static_cast<unsigned>(g_outputs.count()), static_cast<unsigned>(hw::kInputCount),
             static_cast<unsigned>(hw::kOutputCount));
    }
}

void setup_imu(hal::IBoard& board) {
    static const char* const kThread[] = {"imu0", "imu1", "imu2", "imu3"};
    for (std::size_t i = 0; i < hw::kImuCount; ++i) {
        svc::HdlcImu::Config c;
        c.format       = hw::kImuFormat[i];
        c.port         = hw::imu_port_config(i);
        c.software_fcs = hw::kImuSoftwareFcs;
        c.thread_name  = kThread[i % 4];
        c.priority     = kImuPriority;
        c.stack_bytes  = 3072;
        if (!core::is_ok(g_imu[i].init(board.hdlc(i), c))) {
            LOGW(kTag, "IMU%u: no SDLC/HDLC port %u on this board", static_cast<unsigned>(i), static_cast<unsigned>(i));
        }
    }
}

// Bộ nhớ cho Event::buffer: chỉ pool tĩnh (O(1), không phân mảnh, không heap). Sức chứa tính theo
// header 24 B trên host 64-bit. Xem mức dùng/thất bại bằng "sys buffers".
void setup_buffers() {
    static msg::StaticBlockPool<64, 32> tiny;      // BringupStatus, STATUSTEXT ngắn (loạt thông báo lúc khởi động)
    static msg::StaticBlockPool<128, 64> small;    // ImuSample, STATUSTEXT, lệnh GNSS
    static msg::StaticBlockPool<256, 16> medium;   // bảng loopback 12-16 cổng
    static msg::StaticBlockPool<1024, 16> large;   // gói dữ liệu GNSS (<= 512 B)
    static msg::PoolAllocator pools({&tiny, &small, &medium, &large});
    msg::Buffer::set_default_allocator(&pools);
}

void setup_tasks(app::TaskPlan& t) {
    t.app  = osal::ThreadConfig{"app", kAppPriority, 4096, nullptr};
    t.leds = osal::ThreadConfig{"led", osal::Priority::Low, 1024, nullptr};
}

core::Status setup_modules(app::ModuleManager& modules) {
    static app::SerialBringup serial(app::SerialBringup::Config{&g_ports, hw::kGcsPort, hw::kGnssPort});
    static app::IoBringup io(&g_inputs, &g_outputs);
    static device::IImu* const imus[hw::kImuCount] = {&g_imu[0], &g_imu[1]};
    static app::ImuBringup imu(imus, hw::kImuCount);

    // Worker ghi lệnh GNSS: cùng mức ưu tiên với cổng GNSS (lệnh cấu hình không bị các cổng loopback chặn).
    static const osal::ThreadConfig gnss_tx{"gnss_tx", kGnssPortPriority, 2048, nullptr};
    core::Status st = modules.add(&serial, &gnss_tx);
    if (core::is_ok(st)) st = modules.add(&io);
    if (core::is_ok(st)) st = modules.add(&imu);
    return st;
}

// Ánh xạ Event <-> bản tin dùng chung cho mọi kênh MAVLink (không có trạng thái).
BringupDialect& dialect() {
    static BringupDialect d;
    return d;
}

#if EP_HAS_NET
// Kênh MAVLink thứ hai qua UDP: phần mềm giao diện kết nối qua mạng (QGroundControl: UDP link tới
// <ip>:kGcsUdpPort). Mỗi kênh một instance giao thức; telemetry được gửi trên cả hai kênh.
core::Status setup_network_link(app::ProductSetup& s) {
    if (hw::kGcsUdpPort == 0) return core::Status::Ok;
    static comm::UdpStream udp;
    const core::Status st = udp.open(hw::kGcsUdpPort);
    if (!core::is_ok(st)) {
        LOGW(kTag, "UDP %u unavailable (%s): GUI over network disabled", static_cast<unsigned>(hw::kGcsUdpPort),
             core::to_string(st));
        return core::Status::Ok;   // không chặn bring-up: cổng serial vẫn dùng được
    }
    static mavlink2::Mavlink2Protocol udp_mavlink(dialect());
    LOGI(kTag, "GUI link also on UDP port %u", static_cast<unsigned>(hw::kGcsUdpPort));
    return s.links.add_channel("gcs_udp", &udp, {&udp_mavlink}, comm::proto::kMavlink, kGcsLinkPriority, 4096);
}
#else
core::Status setup_network_link(app::ProductSetup&) { return core::Status::Ok; }
#endif

core::Status setup_links(app::ProductSetup& s) {
    // Console (stdin/stdout hoặc UART debug): CLI để chẩn đoán tại chỗ.
    static comm::CliProtocol console_cli;
    core::Status st =
        s.links.add_channel("console", &s.console, {&console_cli}, comm::proto::kCli, kConsolePriority, 4096);
    if (!core::is_ok(st)) return st;

    // Cổng phần mềm giao diện: MAVLink (mặc định); CLI/auto chọn được bằng "link set gcs ...".
    static comm::UartStream gcs;
    hal::IUart* uart = s.board.serial(static_cast<std::size_t>(hw::kGcsPort));
    if (uart == nullptr) {
        LOGE(kTag, "no serial port %d for the GUI link", hw::kGcsPort);
        return core::Status::Ok;
    }
    hal::UartConfig ucfg;
    ucfg.baudrate = hw::kGcsBaudrate;
    st = uart->open(ucfg);
    if (!core::is_ok(st)) LOGE(kTag, "GUI port %d open failed: %s", hw::kGcsPort, core::to_string(st));
    gcs.attach(uart);

    static mavlink2::Mavlink2Protocol gcs_mavlink(dialect());
    static comm::CliProtocol gcs_cli(true);
    st = s.links.add_channel("gcs", &gcs, {&gcs_mavlink, &gcs_cli}, comm::proto::kMavlink, kGcsLinkPriority, 4096);
    if (!core::is_ok(st)) return st;
    return setup_network_link(s);
}

}  // namespace

const char* name() { return "hw_bringup"; }

core::Status setup(app::ProductSetup& s) {
    setup_buffers();
    setup_tasks(s.tasks);
    core::Status st = setup_serial(s.board);
    if (!core::is_ok(st)) return st;
    setup_io(s.board);
    setup_imu(s.board);
    st = setup_modules(s.modules);
    return core::is_ok(st) ? setup_links(s) : st;
}

}  // namespace product
