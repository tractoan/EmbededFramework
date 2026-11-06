#pragma once
// Bảng ánh xạ MAVLink <-> mô hình dữ liệu chuẩn (Event, Param, hành động) – do SẢN PHẨM khai báo.
// Giao thức không biết nghiệp vụ; nghiệp vụ không biết MAVLink.
//
//   Param protocol  : tự động – mọi tham số trong ParamStore (PARAM_REQUEST_LIST/READ/SET <-> PARAM_VALUE)
//   Telemetry       : Event -> NAMED_VALUE_FLOAT
//   StatusText      : Event -> STATUSTEXT (thông báo cho người vận hành)
//   SystemState     : Event -> trường system_status của HEARTBEAT
//   Command         : COMMAND_LONG -> hành động -> COMMAND_ACK
#include <cstddef>
#include <cstdint>

#include "comm/protocol.hpp"

namespace mavlink {

struct Telemetry {
    msg::EventId event;
    const char* name;   // <= 10 ký tự
    bool (*extract)(const msg::Event& e, float& value);
};

struct StatusText {
    msg::EventId event;
    // Ghi severity (mav::kSeverity*) và text (<= 50 ký tự); false = không gửi.
    bool (*format)(const msg::Event& e, std::uint8_t& severity, char* text, std::size_t size);
};

struct SystemState {
    msg::EventId event;
    bool (*state)(const msg::Event& e, std::uint8_t& mav_state);   // mav::kState*
};

struct Command {
    std::uint16_t command;   // MAV_CMD_*
    std::uint8_t (*handle)(comm::Services& services, const float (&params)[7]);   // trả mav::kResult*
};

struct Map {
    std::uint8_t system_id;
    std::uint8_t component_id;
    std::uint8_t mav_type;            // vd. mav::kTypeOnboardController
    std::uint32_t heartbeat_ms;       // chu kỳ HEARTBEAT
    const Telemetry* telemetry;
    std::size_t telemetry_count;
    const StatusText* status_texts;
    std::size_t status_text_count;
    const SystemState* states;
    std::size_t state_count;
    const Command* commands;
    std::size_t command_count;
};

template <typename T, std::size_t N>
constexpr std::size_t count_of(const T (&)[N]) {
    return N;
}

}  // namespace mavlink
