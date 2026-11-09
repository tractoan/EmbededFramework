// Ánh xạ mô hình dữ liệu chuẩn (Event / Param / hành động) -> MAVLink: "cột MAVLink" của sản phẩm.
#include "mavlink_map.hpp"

#include <cstdio>

#include "app_events.hpp"
#include "mavlink/mavlink_v1.hpp"

namespace product {

namespace {
using app::events::AlarmLevel;

bool temperature(const msg::Event& e, float& v) {
    app::events::TemperatureSample s{};
    if (!e.get(s)) return false;
    v = static_cast<float>(s.deci_celsius) / 10.0f;
    return true;
}

bool alarm_text(const msg::Event& e, std::uint8_t& severity, char* text, std::size_t size) {
    app::events::AlarmStateChanged a{};
    if (!e.get(a)) return false;
    switch (a.to) {
        case AlarmLevel::Alarm:
        case AlarmLevel::Fault:   severity = mavlink::mav::kSeverityCritical; break;
        case AlarmLevel::Warning: severity = mavlink::mav::kSeverityWarning; break;
        case AlarmLevel::Normal:  severity = mavlink::mav::kSeverityInfo; break;
    }
    const long t = a.deci_celsius;
    std::snprintf(text, size, "temp alarm %s -> %s (%s%ld.%ld C)", app::events::to_string(a.from),
                  app::events::to_string(a.to), t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
    return true;
}

bool alarm_state(const msg::Event& e, std::uint8_t& state) {
    app::events::AlarmStateChanged a{};
    if (!e.get(a)) return false;
    state = (a.to == AlarmLevel::Alarm || a.to == AlarmLevel::Fault) ? mavlink::mav::kStateCritical
                                                                    : mavlink::mav::kStateActive;
    return true;
}

std::uint8_t ack_alarm(comm::Services& s, const float (&)[7]) {
    s.bus.publish(msg::Event::make(app::events::kAlarmAckRequest));
    return mavlink::mav::kResultAccepted;
}

std::uint8_t storage(comm::Services& s, const float (&p)[7]) {
    if (p[0] == 1.0f) return core::is_ok(s.params.save()) ? mavlink::mav::kResultAccepted : mavlink::mav::kResultFailed;
    if (p[0] == 0.0f) return core::is_ok(s.params.load()) ? mavlink::mav::kResultAccepted : mavlink::mav::kResultFailed;
    return mavlink::mav::kResultDenied;
}

const mavlink::Telemetry kTelemetry[]     = {{app::events::kTemperature, "temp", &temperature}};
const mavlink::StatusText kStatusTexts[]  = {{app::events::kAlarmStateChanged, &alarm_text}};
const mavlink::SystemState kStates[]      = {{app::events::kAlarmStateChanged, &alarm_state}};
const mavlink::Command kCommands[] = {
    {mavlink::mav::kCmdUser1, &ack_alarm},
    {mavlink::mav::kCmdPreflightStorage, &storage},
};

const mavlink::Map kMap{
    1,     // system_id
    191,   // component_id
    mavlink::mav::kTypeOnboardController,
    1000,  // heartbeat_ms
    kTelemetry,   mavlink::count_of(kTelemetry),
    kStatusTexts, mavlink::count_of(kStatusTexts),
    kStates,      mavlink::count_of(kStates),
    kCommands,    mavlink::count_of(kCommands),
};
}  // namespace

const mavlink::Map& mavlink_map() { return kMap; }

}  // namespace product
