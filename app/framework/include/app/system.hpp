#pragma once
#include <atomic>

#include "app/module_manager.hpp"
#include "app/product.hpp"
#include "comm/link_manager.hpp"
#include "device/tmp102.hpp"
#include "hal/board.hpp"
#include "services/command_service.hpp"
#include "services/logger.hpp"
#include "services/param_cli.hpp"

namespace app {

using ProductSetupFn = core::Status (*)(ProductSetup& setup);

// Composition Root: nơi DUY NHẤT "lắp ráp" Platform + Services + Communication + Business.
// Trình tự boot:
//   board.init -> logger -> services -> link manager -> devices
//   -> product::setup (module + kênh/giao thức) -> modules.on_init
//   -> params.load -> services.start -> modules.on_start -> links.start -> kSystemStarted
class System : core::NonCopyable {
public:
    explicit System(hal::IBoard& board) : board_(board), sys_cli_(*this) {}

    core::Status init(const char* product_name, ProductSetupFn setup);
    void shutdown();

    void request_shutdown() { shutdown_req_ = true; }
    bool shutdown_requested() const { return shutdown_req_.load(); }
    SystemContext& context() { return ctx_; }

private:
    void bring_up_devices();

    class SysCli final : public svc::ICommandHandler {
    public:
        explicit SysCli(System& sys) : sys_(sys) {}
        int on_command(int argc, char** argv, svc::CommandOutput& out) override;

    private:
        System& sys_;
    };

    hal::IBoard& board_;
    svc::UartLogSink log_sink_;
    svc::EventBus bus_;
    svc::ParamStore params_;
    svc::CommandService commands_;
    svc::ParamCli param_cli_;
    svc::LedService leds_;
    device::Tmp102 tmp102_;
    Devices devices_;
    ModuleManager modules_;
    comm::UartStream console_stream_;
    comm::Services comm_services_{bus_, params_, commands_};
    comm::LinkManager links_;
    SystemContext ctx_{bus_, params_, commands_, leds_, devices_};
    SysCli sys_cli_;
    const char* product_ = "";
    std::atomic<bool> shutdown_req_{false};
};

}  // namespace app
