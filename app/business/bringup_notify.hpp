#pragma once
// Thông báo cho người vận hành: ghi log cục bộ (console) + phát kStatusText (giao thức gửi lên giao diện).
#include <cstdarg>
#include <cstdio>

#include "app/system_context.hpp"
#include "bringup_events.hpp"
#include "core/compiler.hpp"
#include "services/logger.hpp"

namespace app::bringup {

inline void notify(SystemContext& ctx, const char* tag, Severity sev, const char* fmt, ...) EP_PRINTF_FMT(4, 5);
inline void notify(SystemContext& ctx, const char* tag, Severity sev, const char* fmt, ...) {
    char text[kStatusTextMax + 1];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (sev <= Severity::Error) {
        LOGE(tag, "%s", text);
    } else if (sev == Severity::Warning) {
        LOGW(tag, "%s", text);
    } else {
        LOGI(tag, "%s", text);
    }
    const std::size_t len = static_cast<std::size_t>(n) < kStatusTextMax ? static_cast<std::size_t>(n) : kStatusTextMax;
    ctx.bus.publish(msg::Event::make(kStatusText, StatusHeader{sev}, msg::Buffer::copy_of(text, len)));
}

}  // namespace app::bringup
