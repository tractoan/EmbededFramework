#pragma once
#include <array>
#include <atomic>
#include <cstdarg>

#include "core/compiler.hpp"
#include "core/config.hpp"
#include "hal/uart.hpp"
#include "osal/mutex.hpp"

namespace svc {

enum class LogLevel : std::uint8_t { Trace = 0, Debug, Info, Warn, Error, None };

// Đích ghi log (UART, RTT, file, ring buffer RAM, network...).
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const char* text, std::size_t len) = 0;
};

class UartLogSink final : public ILogSink {
public:
    void attach(hal::IUart* uart) { uart_ = uart; }
    void write(const char* text, std::size_t len) override;

private:
    hal::IUart* uart_ = nullptr;
};

// Logger thread-safe, không cấp phát động. KHÔNG gọi từ ISR.
class Logger : core::NonCopyable {
public:
    static Logger& instance();

    core::Status add_sink(ILogSink* sink);
    void set_level(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }

    void log(LogLevel level, const char* tag, const char* fmt, ...) EP_PRINTF_FMT(4, 5);
    void vlog(LogLevel level, const char* tag, const char* fmt, va_list args);

private:
    Logger() = default;
    osal::Mutex mtx_;
    std::array<ILogSink*, core::cfg::kLogSinks> sinks_{};
    std::size_t sink_count_ = 0;
    std::atomic<LogLevel> level_{LogLevel::Info};
    char line_[core::cfg::kLogLineMax]{};
};

}  // namespace svc

// Log dưới EP_CFG_LOG_LEVEL bị loại bỏ hoàn toàn khi biên dịch.
#define EP_LOG(level, tag, ...)                                              \
    do {                                                                     \
        if (static_cast<int>(level) >= EP_CFG_LOG_LEVEL)                     \
            ::svc::Logger::instance().log(level, tag, __VA_ARGS__);          \
    } while (0)
#define LOGD(tag, ...) EP_LOG(::svc::LogLevel::Debug, tag, __VA_ARGS__)
#define LOGI(tag, ...) EP_LOG(::svc::LogLevel::Info, tag, __VA_ARGS__)
#define LOGW(tag, ...) EP_LOG(::svc::LogLevel::Warn, tag, __VA_ARGS__)
#define LOGE(tag, ...) EP_LOG(::svc::LogLevel::Error, tag, __VA_ARGS__)
