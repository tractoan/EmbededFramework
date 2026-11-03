// Entry point cho Linux (mô phỏng). Dùng: ./firmware [--run-for <giây>]
#include <csignal>
#include <cstdlib>
#include <cstring>

#include "app/product.hpp"
#include "app/system.hpp"
#include "osal/time.hpp"

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }
}  // namespace

int main(int argc, char** argv) {
    std::uint32_t run_for_s = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--run-for") == 0 && i + 1 < argc) run_for_s = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    static app::System system(hal::board());
    if (!core::is_ok(system.init(product::name(), &product::register_modules))) return EXIT_FAILURE;

    const std::uint64_t deadline = run_for_s ? osal::now_ms() + run_for_s * 1000ull : 0;
    while (!g_stop && !system.shutdown_requested() && (deadline == 0 || osal::now_ms() < deadline)) {
        osal::sleep_ms(50);
    }
    system.shutdown();
    return EXIT_SUCCESS;
}
