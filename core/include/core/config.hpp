#pragma once
#include <cstddef>

// Cấu hình tĩnh toàn hệ thống. Mỗi sản phẩm có thể override bằng
// products/<product>/config/product_config.hpp (định nghĩa macro trước khi dùng mặc định).
#if defined(__has_include)
#if __has_include("product_config.hpp")
#include "product_config.hpp"
#endif
#endif

#ifndef EP_CFG_MAX_EVENT_SUBSCRIBERS
#define EP_CFG_MAX_EVENT_SUBSCRIBERS 16
#endif
#ifndef EP_CFG_EVENT_PAYLOAD_SIZE
#define EP_CFG_EVENT_PAYLOAD_SIZE 16
#endif
#ifndef EP_CFG_MAX_MODULES
#define EP_CFG_MAX_MODULES 16
#endif
#ifndef EP_CFG_APP_QUEUE_DEPTH
#define EP_CFG_APP_QUEUE_DEPTH 32
#endif
#ifndef EP_CFG_MAX_PARAMS
#define EP_CFG_MAX_PARAMS 48
#endif
#ifndef EP_CFG_MAX_COMMANDS
#define EP_CFG_MAX_COMMANDS 24
#endif
#ifndef EP_CFG_CLI_LINE_MAX
#define EP_CFG_CLI_LINE_MAX 96
#endif
#ifndef EP_CFG_CLI_MAX_ARGS
#define EP_CFG_CLI_MAX_ARGS 8
#endif
#ifndef EP_CFG_CLI_ECHO
#define EP_CFG_CLI_ECHO 1
#endif
#ifndef EP_CFG_LOG_LINE_MAX
#define EP_CFG_LOG_LINE_MAX 160
#endif
#ifndef EP_CFG_LOG_SINKS
#define EP_CFG_LOG_SINKS 4
#endif
// 0=Trace 1=Debug 2=Info 3=Warn 4=Error 5=None – log thấp hơn bị loại bỏ lúc biên dịch
#ifndef EP_CFG_LOG_LEVEL
#define EP_CFG_LOG_LEVEL 1
#endif

namespace core::cfg {
inline constexpr std::size_t kMaxEventSubscribers = EP_CFG_MAX_EVENT_SUBSCRIBERS;
inline constexpr std::size_t kEventPayloadSize    = EP_CFG_EVENT_PAYLOAD_SIZE;
inline constexpr std::size_t kMaxModules          = EP_CFG_MAX_MODULES;
inline constexpr std::size_t kAppQueueDepth       = EP_CFG_APP_QUEUE_DEPTH;
inline constexpr std::size_t kMaxParams           = EP_CFG_MAX_PARAMS;
inline constexpr std::size_t kMaxCommands         = EP_CFG_MAX_COMMANDS;
inline constexpr std::size_t kCliLineMax          = EP_CFG_CLI_LINE_MAX;
inline constexpr std::size_t kCliMaxArgs          = EP_CFG_CLI_MAX_ARGS;
inline constexpr bool        kCliEcho             = EP_CFG_CLI_ECHO != 0;
inline constexpr std::size_t kLogLineMax          = EP_CFG_LOG_LINE_MAX;
inline constexpr std::size_t kLogSinks            = EP_CFG_LOG_SINKS;
}  // namespace core::cfg
