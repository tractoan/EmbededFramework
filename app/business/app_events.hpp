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
inline constexpr EventId kTemperatureBatch  = svc::evt::kAppBase + 6;   // TemperatureBatch + buffer: int16[count]

// Header inline của kTemperatureBatch; dữ liệu nằm trong Event::buffer:
// `count` mẫu int16 (0.1 °C), theo thứ tự thời gian, cách nhau `period_ms`.
struct TemperatureBatch {
    std::uint16_t count;
    std::uint16_t period_ms;
    std::uint32_t first_sample_ms;
};

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
