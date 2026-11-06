#include "app/system.hpp"

#include <cstdlib>
#include <cstring>

#include "osal/time.hpp"

namespace app {

namespace {
constexpr const char* kTag = "system";
}

core::Status System::init(const char* product_name, ProductSetupFn setup) {
    product_ = product_name;

    // 1. Phần cứng + log sớm nhất có thể
    core::Status st = board_.init();
    if (!core::is_ok(st)) return st;
    hal::IUart* console = board_.uart(hal::UartId::Console);
    log_sink_.attach(console);
    svc::Logger::instance().add_sink(&log_sink_);
    LOGI(kTag, "boot: product='%s' board='%s'", product_, board_.name());

    // 2. Services
    params_.init(board_.storage(), &bus_);
    param_cli_.init(&params_);
    commands_.register_command({"param", "list | get <n> | set <n> <v> | save | reset", &param_cli_});
    commands_.register_command({"sys", "info | modules | log <0-5> | reboot | quit", &sys_cli_});
    leds_.bind(svc::LedId::Status, board_.gpio(hal::GpioId::StatusLed));
    leds_.bind(svc::LedId::Alarm, board_.gpio(hal::GpioId::AlarmLed));

    // 3. Communication: kênh & giao thức do sản phẩm khai báo ở bước 5
    console_stream_.attach(console);
    st = links_.init(comm_services_);
    if (!core::is_ok(st)) return st;

    // 4. Thiết bị (driver gắn lên bus HAL)
    bring_up_devices();

    // 5. Sản phẩm: module nghiệp vụ + kênh truyền thông
    if (setup == nullptr) return core::Status::InvalidArg;
    ProductSetup product_setup{modules_, links_, board_, console_stream_};
    st = setup(product_setup);
    if (!core::is_ok(st)) {
        LOGE(kTag, "product setup failed: %s", core::to_string(st));
        return st;
    }
    st = modules_.init_all(ctx_);
    if (!core::is_ok(st)) return st;

    // 6. Nạp cấu hình đã lưu (gồm cả lựa chọn giao thức của từng kênh) (sau khi mọi module đã đăng ký tham số)
    st = params_.load();
    if (core::is_ok(st)) {
        LOGI(kTag, "parameters loaded from NV (%u)", static_cast<unsigned>(params_.count()));
    } else {
        LOGW(kTag, "NV params unavailable (%s) -> defaults", core::to_string(st));
    }

    // 7. Chạy
    leds_.start();
    leds_.set(svc::LedId::Status, svc::LedPattern::Heartbeat);
    st = modules_.start();
    if (!core::is_ok(st)) return st;
    st = links_.start();
    if (!core::is_ok(st)) return st;

    bus_.publish(svc::Event::make(svc::evt::kSystemStarted));
    LOGI(kTag, "system started");
    return core::Status::Ok;
}

void System::bring_up_devices() {
    if (hal::II2c* i2c = board_.i2c(hal::I2cId::Sensor)) {
        tmp102_.init(i2c, 0x48);
        devices_.temperature = &tmp102_;
    } else {
        LOGW(kTag, "no sensor bus on this board");
    }
}

void System::shutdown() {
    LOGI(kTag, "shutting down...");
    bus_.publish(svc::Event::make(svc::evt::kSystemShutdown));
    links_.stop();
    modules_.stop();
    leds_.set(svc::LedId::Status, svc::LedPattern::Off);
    leds_.set(svc::LedId::Alarm, svc::LedPattern::Off);
    leds_.tick(osal::now_ms());
    leds_.stop();
    LOGI(kTag, "halted");
}

int System::SysCli::on_command(int argc, char** argv, svc::CommandOutput& out) {
    const char* sub = argc >= 2 ? argv[1] : "info";
    if (std::strcmp(sub, "info") == 0) {
        const std::uint64_t up = osal::now_ms();
        out.print("product : %s\r\nboard   : %s\r\nuptime  : %lu.%03lu s\r\nmodules : %u\r\ndropped : %lu events\r\n",
                  sys_.product_, sys_.board_.name(), static_cast<unsigned long>(up / 1000),
                  static_cast<unsigned long>(up % 1000), static_cast<unsigned>(sys_.modules_.count()),
                  static_cast<unsigned long>(sys_.modules_.dropped_events()));
        return 0;
    }
    if (std::strcmp(sub, "modules") == 0) {
        for (std::size_t i = 0; i < sys_.modules_.count(); ++i) {
            const IModule* m = sys_.modules_.at(i);
            out.print("  [%u] %-16s tick=%lu ms\r\n", static_cast<unsigned>(i), m->name(),
                      static_cast<unsigned long>(m->tick_period_ms()));
        }
        return 0;
    }
    if (std::strcmp(sub, "log") == 0 && argc >= 3) {
        const int lvl = std::atoi(argv[2]);
        if (lvl < 0 || lvl > 5) return -1;
        svc::Logger::instance().set_level(static_cast<svc::LogLevel>(lvl));
        out.print("log level = %d\r\n", lvl);
        return 0;
    }
    if (std::strcmp(sub, "reboot") == 0) sys_.board_.reset();
    if (std::strcmp(sub, "quit") == 0) {
        sys_.request_shutdown();
        return 0;
    }
    out.print("usage: sys info | modules | log <0-5> | reboot | quit\r\n");
    return -1;
}

}  // namespace app
