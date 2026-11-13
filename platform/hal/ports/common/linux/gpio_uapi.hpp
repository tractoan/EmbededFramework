#pragma once
// Lớp tương thích GPIO character device của Linux: uAPI v2 (kernel/header >= 5.10) hoặc v1 (>= 4.8).
//  - Header kernel có v2 (GPIO_V2_GET_LINE_IOCTL): dùng v2; kernel lúc chạy cũ hơn (ioctl trả ENOTTY) -> tự lùi về v1.
//  - Header chỉ có v1 (vd. SDK/sysroot cũ): chỉ dùng v1.
//  - Ép dùng v1 khi build: -DEP_GPIO_UAPI_V1_ONLY (kiểm thử nhánh v1 trên máy mới).
// Kéo lên/kéo xuống (bias): v2 luôn có; v1 chỉ khi header >= 5.5 (GPIOHANDLE_REQUEST_BIAS_*), không có thì bỏ qua.
#include <linux/gpio.h>
#include <sys/ioctl.h>

#include <cerrno>
#include <cstring>

#if defined(GPIO_V2_GET_LINE_IOCTL) && !defined(EP_GPIO_UAPI_V1_ONLY)
#define EP_GPIO_HAVE_UAPI_V2 1
#else
#define EP_GPIO_HAVE_UAPI_V2 0
#endif

namespace hal::host::gpio {

enum class Bias : unsigned char { None, PullUp, PullDown };

struct LineRequest {
    unsigned offset      = 0;
    bool output          = false;
    bool initial         = false;   // mức ban đầu của output
    bool active_low      = false;
    bool open_drain      = false;
    Bias bias            = Bias::None;
    const char* consumer = "ep";
};

// Xin một line trên chip đã mở. Trả fd của line (>= 0) và abi (1 hoặc 2) dùng cho get/set; -1 nếu lỗi (errno).
inline int request_line(int chip_fd, const LineRequest& r, int& abi) {
#if EP_GPIO_HAVE_UAPI_V2
    gpio_v2_line_request req{};
    req.offsets[0] = r.offset;
    req.num_lines  = 1;
    std::strncpy(req.consumer, r.consumer ? r.consumer : "ep", sizeof(req.consumer) - 1);
    req.config.flags = r.output ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;
    if (r.open_drain) req.config.flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
    if (r.active_low) req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
    if (r.bias == Bias::PullUp) req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    if (r.bias == Bias::PullDown) req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
    if (r.output) {   // mức ban đầu đặt ngay khi xin line (không có xung sai mức)
        req.config.num_attrs            = 1;
        req.config.attrs[0].attr.id     = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        req.config.attrs[0].attr.values = r.initial ? 1u : 0u;
        req.config.attrs[0].mask        = 1;
    }
    if (::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) == 0) {
        abi = 2;
        return req.fd;
    }
    if (errno != ENOTTY) return -1;   // kernel có v2 nhưng từ chối (line bận, offset sai...) -> lỗi thật
    // ENOTTY: kernel < 5.10 không có v2 -> thử v1
#endif
    gpiohandle_request h{};
    h.lineoffsets[0] = r.offset;
    h.lines          = 1;
    h.flags          = r.output ? GPIOHANDLE_REQUEST_OUTPUT : GPIOHANDLE_REQUEST_INPUT;
    if (r.open_drain) h.flags |= GPIOHANDLE_REQUEST_OPEN_DRAIN;
    if (r.active_low) h.flags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;
#if defined(GPIOHANDLE_REQUEST_BIAS_PULL_UP)
    if (r.bias == Bias::PullUp) h.flags |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
    if (r.bias == Bias::PullDown) h.flags |= GPIOHANDLE_REQUEST_BIAS_PULL_DOWN;
#endif
    h.default_values[0] = r.initial ? 1 : 0;
    std::strncpy(h.consumer_label, r.consumer ? r.consumer : "ep", sizeof(h.consumer_label) - 1);
    if (::ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &h) != 0) return -1;
    abi = 1;
    return h.fd;
}

inline bool get_value(int line_fd, int abi, bool& level) {
#if EP_GPIO_HAVE_UAPI_V2
    if (abi == 2) {
        gpio_v2_line_values v{};
        v.mask = 1;
        if (::ioctl(line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) != 0) return false;
        level = (v.bits & 1u) != 0;
        return true;
    }
#endif
    (void)abi;
    gpiohandle_data d{};
    if (::ioctl(line_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &d) != 0) return false;
    level = d.values[0] != 0;
    return true;
}

inline bool set_value(int line_fd, int abi, bool level) {
#if EP_GPIO_HAVE_UAPI_V2
    if (abi == 2) {
        gpio_v2_line_values v{};
        v.mask = 1;
        v.bits = level ? 1u : 0u;
        return ::ioctl(line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) == 0;
    }
#endif
    (void)abi;
    gpiohandle_data d{};
    d.values[0] = level ? 1 : 0;
    return ::ioctl(line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d) == 0;
}

}  // namespace hal::host::gpio
