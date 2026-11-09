#pragma once
// Cấu hình tĩnh cho sản phẩm hw_bringup (firmware chạy trên CPU). Dữ liệu lớn (IMU, bảng loopback,
// GNSS, STATUSTEXT) đi qua Event::buffer (pool tĩnh ở product.cpp) nên payload inline giữ mặc định 16 B.
#define EP_CFG_APP_QUEUE_DEPTH 128      // luồng GNSS tốc độ cao đi qua bus
#define EP_CFG_MAX_MODULES 8
#define EP_CFG_MAX_PARAMS 48
#define EP_CFG_MAX_COMMANDS 24
#define EP_CFG_COMM_MAX_CHANNELS 4
// Task: console 4K + gcs 4K + gcs_udp 4K + app 4K + led 1K + 11 cổng serial x 2K + 2 IMU x 3K = 45 KB (+ dự phòng)
#define EP_CFG_STACK_POOL_BYTES (56u * 1024u)
#define EP_CFG_MAX_THREADS 24
#define EP_CFG_LOG_LEVEL 1
