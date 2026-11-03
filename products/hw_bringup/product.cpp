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
#include "mavlink/mavlink_protocol.hpp"
#include "mavlink_dialect.hpp"
#include "serial_bringup/serial_bringup.hpp"
#include "services/drivers/digital_io.hpp"
#include "services/drivers/hdlc_imu.hpp"
#include "services/logger.hpp"
#include "services/serial_port_bank.hpp"

namespace product {

namespace {
constexpr const char* kTag = "bringup";

// Thiết bị/dịch vụ của sản phẩm – cấp phát tĩnh.
svc::SerialPortBank g_ports;
svc::GpioInputBank g_inputs;
svc::GpioOutputBank g_outputs;
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
        if (!core::is_ok(g_imu[i].init(board.hdlc(i), c))) {
            LOGW(kTag, "IMU%u: no SDLC/HDLC port %u on this board", static_cast<unsigned>(i), static_cast<unsigned>(i));
        }
    }
}

core::Status setup_modules(app::ModuleManager& modules) {
    static app::SerialBringup serial(app::SerialBringup::Config{&g_ports, hw::kGcsPort, hw::kGnssPort});
    static app::IoBringup io(&g_inputs, &g_outputs);
    static svc::IImu* const imus[hw::kImuCount] = {&g_imu[0], &g_imu[1]};
    static app::ImuBringup imu(imus, hw::kImuCount);

    core::Status st = modules.add(&serial);
    if (core::is_ok(st)) st = modules.add(&io);
    if (core::is_ok(st)) st = modules.add(&imu);
    return st;
}

core::Status setup_links(app::ProductSetup& s) {
    // Console (stdin/stdout hoặc UART debug): CLI để chẩn đoán tại chỗ.
    static comm::CliProtocol console_cli;
    core::Status st = s.links.add_channel("console", &s.console, {&console_cli}, comm::proto::kCli);
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

    static BringupDialect dialect;
    static mavlink::MavlinkProtocol gcs_mavlink(dialect);
    static comm::CliProtocol gcs_cli(true);
    return s.links.add_channel("gcs", &gcs, {&gcs_mavlink, &gcs_cli}, comm::proto::kMavlink);
}

}  // namespace

const char* name() { return "hw_bringup"; }

core::Status setup(app::ProductSetup& s) {
    core::Status st = setup_serial(s.board);
    if (!core::is_ok(st)) return st;
    setup_io(s.board);
    setup_imu(s.board);
    st = setup_modules(s.modules);
    return core::is_ok(st) ? setup_links(s) : st;
}

}  // namespace product
