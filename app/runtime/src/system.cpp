#include "app/system.hpp"

#include <cstdlib>
#include <cstring>

#include "osal/memory.hpp"
#include "osal/time.hpp"
#include "messaging/buffer.hpp"

namespace app {

namespace {
constexpr const char* kTag = "system";

core::Status task_start_failed(const char* what, core::Status st) {
    const osal::StackPoolStats sp = osal::stack_pool_stats();
    LOGE(kTag, "cannot start %s task(s): %s (stack pool %lu/%lu B used) - check TaskPlan / EP_CFG_STACK_POOL_BYTES",
         what, core::to_string(st), static_cast<unsigned long>(sp.used), static_cast<unsigned long>(sp.capacity));
    return st;
}
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
    commands_.register_command({"sys", "info | modules | tasks | buffers | log <0-5> | reboot | quit", &sys_cli_});
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
    ProductSetup product_setup{modules_, links_, board_, console_stream_, tasks_};
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
    st = leds_.start(tasks_.leds);
    if (!core::is_ok(st)) return task_start_failed("led", st);
    leds_.set(svc::LedId::Status, svc::LedPattern::Heartbeat);
    // Từ đây đã có thread đang chạy: lỗi khởi động phải dừng sạch chúng trước khi trả về,
    // nếu không, thread còn sống sẽ khiến việc hủy System bị treo.
    st = modules_.start(tasks_.app);
    if (core::is_ok(st)) {
        st = links_.start();
        if (!core::is_ok(st)) task_start_failed("comm channels", st);
    } else {
        task_start_failed("app", st);
    }
    if (!core::is_ok(st)) {
        shutdown();
        return st;
    }

    bus_.publish(msg::Event::make(app::sys::kSystemStarted));
    // 8. Hết giai đoạn khởi động: mọi thread đã tạo, mọi bộ nhớ đã cấp.
    //    Từ đây cấp phát heap / lấy stack mới là vi phạm (xem osal/memory.hpp).
    osal::end_startup();
    const osal::StackPoolStats sp = osal::stack_pool_stats();
    LOGI(kTag, "system started: %u threads, stack pool %lu/%lu B, heap sealed",
         static_cast<unsigned>(osal::thread_count()), static_cast<unsigned long>(sp.used),
         static_cast<unsigned long>(sp.capacity));
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
    bus_.publish(msg::Event::make(app::sys::kSystemShutdown));
    links_.stop();
    modules_.stop();
    leds_.set(svc::LedId::Status, svc::LedPattern::Off);
    leds_.set(svc::LedId::Alarm, svc::LedPattern::Off);
    leds_.stop();                 // dừng thread LED trước, rồi áp trạng thái Off trên thread này
    leds_.tick(osal::now_ms());
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
            IModule* m = sys_.modules_.at(i);
            out.print("  [%u] %-16s tick=%lu ms", static_cast<unsigned>(i), m->name(),
                      static_cast<unsigned long>(m->tick_period_ms()));
            if (const ModuleWorkerBase* w = m->worker()) {
                out.print("  worker=%s jobs=%lu dropped=%lu ticks=%lu queue=%u", w->config().name,
                          static_cast<unsigned long>(w->processed()), static_cast<unsigned long>(w->dropped()),
                          static_cast<unsigned long>(w->ticks()), static_cast<unsigned>(w->depth()));
            }
            out.print("\r\n");
        }
        return 0;
    }
    if (std::strcmp(sub, "tasks") == 0) {
        osal::ThreadInfo t{};
        for (std::size_t i = 0; osal::thread_info(i, t); ++i) {
            out.print("  %-10s prio=%2u stack=%5lu B %s\r\n", t.name, static_cast<unsigned>(t.priority.level()),
                      static_cast<unsigned long>(t.stack_bytes), t.pooled_stack ? "(pool)" : "");
        }
        const osal::StackPoolStats sp = osal::stack_pool_stats();
        out.print("stack pool: %lu/%lu B used, %lu failures\r\nheap: %s, %lu violations\r\n",
                  static_cast<unsigned long>(sp.used), static_cast<unsigned long>(sp.capacity),
                  static_cast<unsigned long>(sp.failures), osal::startup_complete() ? "sealed" : "startup",
                  static_cast<unsigned long>(osal::heap_violations()));
        return 0;
    }
    if (std::strcmp(sub, "buffers") == 0) {
        const msg::IBufferAllocator* a = msg::Buffer::default_allocator();
        if (a == nullptr) {
            out.print("no buffer allocator configured\r\n");
            return 0;
        }
        for (std::size_t i = 0; i < a->pool_count(); ++i) {
            const msg::PoolStats p = a->pool_stats(i);
            out.print("  pool %5lu B used=%lu/%lu  peak=%lu  failures=%lu\r\n", static_cast<unsigned long>(p.block_size),
                      static_cast<unsigned long>(p.used), static_cast<unsigned long>(p.capacity),
                      static_cast<unsigned long>(p.peak), static_cast<unsigned long>(p.failures));
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
    out.print("usage: sys info | modules | tasks | buffers | log <0-5> | reboot | quit\r\n");
    return -1;
}

}  // namespace app
