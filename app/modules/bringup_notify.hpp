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
    StatusText t{};
    t.severity = sev;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(t.text, sizeof(t.text), fmt, ap);
    va_end(ap);
    if (sev <= Severity::Error) {
        LOGE(tag, "%s", t.text);
    } else if (sev == Severity::Warning) {
        LOGW(tag, "%s", t.text);
    } else {
        LOGI(tag, "%s", t.text);
    }
    ctx.bus.publish(svc::Event::make(kStatusText, t));
}

}  // namespace app::bringup
