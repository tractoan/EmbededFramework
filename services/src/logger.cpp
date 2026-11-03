#include "services/logger.hpp"

#include <cstdio>

#include "osal/time.hpp"

namespace svc {

void UartLogSink::write(const char* text, std::size_t len) {
    if (uart_ != nullptr) uart_->write(reinterpret_cast<const std::uint8_t*>(text), len, 100);
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

core::Status Logger::add_sink(ILogSink* sink) {
    if (sink == nullptr) return core::Status::InvalidArg;
    osal::LockGuard g(mtx_);
    if (sink_count_ >= sinks_.size()) return core::Status::NoMemory;
    sinks_[sink_count_++] = sink;
    return core::Status::Ok;
}

void Logger::log(LogLevel level, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(level, tag, fmt, ap);
    va_end(ap);
}

void Logger::vlog(LogLevel level, const char* tag, const char* fmt, va_list args) {
    if (level < level_.load() || level >= LogLevel::None) return;
    static constexpr char kLetter[] = {'T', 'D', 'I', 'W', 'E'};
    static constexpr const char* kColor[] = {"\033[90m", "\033[36m", "", "\033[33m", "\033[31m"};
    const auto idx = static_cast<std::size_t>(level);

    osal::LockGuard g(mtx_);
    const std::uint64_t ts = osal::now_ms();
    constexpr std::size_t kCap = sizeof(line_) - 7;   // chừa chỗ cho reset màu + "\r\n"
    int n = std::snprintf(line_, kCap, "%s[%6lu.%03lu] %c/%-12s ", kColor[idx],
                          static_cast<unsigned long>(ts / 1000), static_cast<unsigned long>(ts % 1000), kLetter[idx],
                          tag);
    if (n < 0) return;
    std::size_t len = static_cast<std::size_t>(n) < kCap ? static_cast<std::size_t>(n) : kCap - 1;
    const int m = std::vsnprintf(line_ + len, kCap - len, fmt, args);
    if (m > 0) len = (len + static_cast<std::size_t>(m) < kCap) ? len + static_cast<std::size_t>(m) : kCap - 1;
    len += static_cast<std::size_t>(std::snprintf(line_ + len, sizeof(line_) - len, "\033[0m\r\n"));

    for (std::size_t i = 0; i < sink_count_; ++i) sinks_[i]->write(line_, len);
}

}  // namespace svc
