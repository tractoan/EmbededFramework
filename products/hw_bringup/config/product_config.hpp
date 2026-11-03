#pragma once
// Cấu hình tĩnh cho sản phẩm hw_bringup (firmware chạy trên CPU – RAM dồi dào).
#define EP_CFG_EVENT_PAYLOAD_SIZE 128   // event bring-up (LoopbackStatus, ImuSample...) tới 128 byte
#define EP_CFG_APP_QUEUE_DEPTH 128      // luồng GNSS tốc độ cao đi qua bus
#define EP_CFG_MAX_MODULES 8
#define EP_CFG_MAX_PARAMS 48
#define EP_CFG_MAX_COMMANDS 24
#define EP_CFG_COMM_MAX_CHANNELS 4
#define EP_CFG_LOG_LEVEL 1
