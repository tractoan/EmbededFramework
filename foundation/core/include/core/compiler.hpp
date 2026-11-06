#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define EP_PRINTF_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#define EP_WEAK __attribute__((weak))
#else
#define EP_PRINTF_FMT(fmt_idx, args_idx)
#define EP_WEAK
#endif
