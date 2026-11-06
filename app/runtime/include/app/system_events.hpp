#pragma once
// Event vòng đời hệ thống do app::System phát (dải msg::evt::kSystemBase).
#include "messaging/event.hpp"

namespace app::sys {

inline constexpr msg::EventId kSystemStarted  = msg::evt::kSystemBase + 1;   // (none) – sau khi mọi task đã chạy
inline constexpr msg::EventId kSystemShutdown = msg::evt::kSystemBase + 2;   // (none) – trước khi dừng task

}  // namespace app::sys
