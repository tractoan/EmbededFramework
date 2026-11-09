// Target Zynq UltraScale+ APU (Cortex-A53) + Linux. Tham số: ./firmware [--run-for <giây>]
// Khác bản mô phỏng: khóa bộ nhớ (mlockall) và kiểm tra quyền realtime để priority task có tác dụng.
#include <sched.h>
#include <sys/mman.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "osal/memory.hpp"
#include "osal/time.hpp"
#include "target/target.hpp"

namespace target {

namespace {
volatile std::sig_atomic_t g_stop = 0;
std::uint64_t g_deadline_ms       = 0;

void on_signal(int) { g_stop = 1; }

void on_heap_violation(std::size_t bytes) {
    std::fprintf(stderr, "FATAL: heap allocation of %zu bytes after startup\n", bytes);
    std::abort();
}
}  // namespace

void init(int argc, char** argv) {
    osal::set_heap_violation_handler(&on_heap_violation);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--run-for") == 0 && i + 1 < argc) {
            g_deadline_ms = osal::now_ms() + static_cast<std::uint64_t>(std::atoi(argv[++i])) * 1000u;
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // Realtime trên Linux: khóa toàn bộ bộ nhớ (kể cả stack thread sẽ tạo) để không có page fault
    // trong đường đi thời gian thực. Không có quyền thì vẫn chạy, chỉ mất tính realtime.
    if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::fputs("[zynqmp] warning: mlockall failed (need CAP_IPC_LOCK) - memory may be paged\n", stderr);
    }
    sched_param sp{};
    sp.sched_priority = 1;
    if (::sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        std::fputs("[zynqmp] warning: no realtime privileges (need CAP_SYS_NICE) - task priorities ignored\n", stderr);
    } else {
        sp.sched_priority = 0;
        ::sched_setscheduler(0, SCHED_OTHER, &sp);   // thread chính không cần realtime
    }
}

int run(AppMain app_main) { return app_main(); }

bool stop_requested() { return g_stop != 0 || (g_deadline_ms != 0 && osal::now_ms() >= g_deadline_ms); }

}  // namespace target
