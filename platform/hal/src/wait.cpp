#include "hal/wait.hpp"

#include <atomic>

namespace hal {

namespace {
std::atomic<WaitFunction> g_wait{nullptr};
}

void set_wait_function(WaitFunction fn) { g_wait.store(fn); }

bool wait_ms(std::uint32_t ms) {
    const WaitFunction fn = g_wait.load();
    if (fn == nullptr) return false;
    fn(ms);
    return true;
}

}  // namespace hal
