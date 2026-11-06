// Target Linux (mô phỏng). Tham số: ./firmware [--run-for <giây>]
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "osal/time.hpp"
#include "target/target.hpp"

namespace target {

namespace {
volatile std::sig_atomic_t g_stop = 0;
std::uint64_t g_deadline_ms       = 0;   // 0 = chạy tới khi Ctrl+C / "sys quit"

void on_signal(int) { g_stop = 1; }
}  // namespace

void init(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--run-for") == 0 && i + 1 < argc) {
            g_deadline_ms = osal::now_ms() + static_cast<std::uint64_t>(std::atoi(argv[++i])) * 1000u;
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
}

int run(AppMain app_main) { return app_main(); }

bool stop_requested() { return g_stop != 0 || (g_deadline_ms != 0 && osal::now_ms() >= g_deadline_ms); }

}  // namespace target
