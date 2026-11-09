// Target QNX Neutrino 7.1 / 8.0 (aarch64le, x86_64). Tham số: ./firmware [--run-for <giây>]
// Như linux_sim + các bước cho realtime: khóa bộ nhớ, kiểm tra quyền priority, bỏ qua SIGPIPE.
#include <pthread.h>
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

#ifndef EP_POSIX_RT_PRIO_BASE
#define EP_POSIX_RT_PRIO_BASE 10
#endif
#ifndef EP_POSIX_RT_PRIO_STEP
#define EP_POSIX_RT_PRIO_STEP 5
#endif

namespace target {

namespace {
volatile std::sig_atomic_t g_stop = 0;
std::uint64_t g_deadline_ms       = 0;

void on_signal(int) { g_stop = 1; }

void on_heap_violation(std::size_t bytes) {
    std::fprintf(stderr, "FATAL: heap allocation of %zu bytes after startup\n", bytes);
    std::abort();
}

// Thử đặt priority cao nhất mà task có thể dùng (mức 15) cho thread chính rồi trả lại:
// cho biết trước priority task có tác dụng không (QNX: > 63 cần PROCMGR_AID_PRIORITY).
void check_realtime_privileges() {
    const int top = EP_POSIX_RT_PRIO_BASE + EP_POSIX_RT_PRIO_STEP * 15;
    if (top > sched_get_priority_max(SCHED_FIFO)) {
        std::fprintf(stderr, "[qnx] warning: task priority %d exceeds SCHED_FIFO max %d - adjust EP_POSIX_RT_PRIO_*\n",
                     top, sched_get_priority_max(SCHED_FIFO));
    }
    int policy = 0;
    sched_param old{};
    if (pthread_getschedparam(pthread_self(), &policy, &old) != 0) return;
    sched_param sp{};
    sp.sched_priority = top;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        std::fputs("[qnx] warning: no privilege for task priorities (run as root or grant PROCMGR_AID_PRIORITY)"
                   " - priorities may be capped\n",
                   stderr);
        return;
    }
    pthread_setschedparam(pthread_self(), policy, &old);   // thread chính không cần realtime
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
    std::signal(SIGPIPE, SIG_IGN);   // client TCP ngắt giữa chừng không được làm chết tiến trình

    // Khóa bộ nhớ: không page fault/lazy allocation trong đường đi thời gian thực.
    if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::fputs("[qnx] warning: mlockall failed (need PROCMGR_AID_MEM_LOCK) - memory may not be locked\n", stderr);
    }
    check_realtime_privileges();
}

int run(AppMain app_main) { return app_main(); }

bool stop_requested() { return g_stop != 0 || (g_deadline_ms != 0 && osal::now_ms() >= g_deadline_ms); }

}  // namespace target
