#pragma once
// Hợp đồng giao tiếp giữa các module nghiệp vụ: chỉ qua Event (loose coupling).
#include <cstdint>

#include "services/event.hpp"

namespace app::events {

using svc::EventId;
inline constexpr EventId kTemperature       = svc::evt::kAppBase + 1;   // TemperatureSample
inline constexpr EventId kSensorFault       = svc::evt::kAppBase + 2;   // (none)
inline constexpr EventId kSensorRecovered   = svc::evt::kAppBase + 3;   // (none)
inline constexpr EventId kAlarmStateChanged = svc::evt::kAppBase + 4;   // AlarmStateChanged
inline constexpr EventId kAlarmAckRequest   = svc::evt::kAppBase + 5;   // (none)

struct TemperatureSample {
    std::int32_t deci_celsius;
};

enum class AlarmLevel : std::uint8_t { Normal, Warning, Alarm, Fault };

struct AlarmStateChanged {
    AlarmLevel from;
    AlarmLevel to;
    std::int32_t deci_celsius;
};

inline const char* to_string(AlarmLevel l) {
    switch (l) {
        case AlarmLevel::Normal:  return "NORMAL";
        case AlarmLevel::Warning: return "WARNING";
        case AlarmLevel::Alarm:   return "ALARM";
        case AlarmLevel::Fault:   return "FAULT";
    }
    return "?";
}

}  // namespace app::events
