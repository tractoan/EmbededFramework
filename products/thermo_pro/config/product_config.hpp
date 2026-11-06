#pragma once
// Override cấu hình tĩnh cho sản phẩm thermo_pro (xem core/config.hpp)
#define EP_CFG_MAX_MODULES 8
#define EP_CFG_APP_QUEUE_DEPTH 32
#define EP_CFG_STACK_POOL_BYTES (16u * 1024u)   // app 4K + led 1K + 2 kênh x 4K = 13 KB
