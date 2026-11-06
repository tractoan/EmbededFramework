#pragma once
#include <cstdint>

namespace core {

// Mã lỗi thống nhất cho mọi lớp (thay cho exception).
enum class Status : std::int8_t {
    Ok           = 0,
    Error        = -1,
    Timeout      = -2,
    Busy         = -3,
    InvalidArg   = -4,
    NotSupported = -5,
    NoMemory     = -6,
    NotFound     = -7,
    OutOfRange   = -8,
    Corrupted    = -9,
};

constexpr bool is_ok(Status s) { return s == Status::Ok; }

constexpr const char* to_string(Status s) {
    switch (s) {
        case Status::Ok:           return "Ok";
        case Status::Error:        return "Error";
        case Status::Timeout:      return "Timeout";
        case Status::Busy:         return "Busy";
        case Status::InvalidArg:   return "InvalidArg";
        case Status::NotSupported: return "NotSupported";
        case Status::NoMemory:     return "NoMemory";
        case Status::NotFound:     return "NotFound";
        case Status::OutOfRange:   return "OutOfRange";
        case Status::Corrupted:    return "Corrupted";
    }
    return "?";
}

}  // namespace core
