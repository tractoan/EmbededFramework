#pragma once
#include "modbus/modbus_map.hpp"

namespace product {

// Bản đồ thanh ghi Modbus của thermo_pro (unit id 1):
//
//  Input registers (FC04)          Holding registers (FC03/06/16)   Coils (FC05)
//  30001 [0] nhiệt độ  0.1°C (s16)  40001 [0] alarm.warn  0.1°C      00001 [0] xác nhận cảnh báo
//  30002 [1] mức cảnh báo 0..3      40002 [1] alarm.high  0.1°C      00002 [1] lưu tham số (Flash)
//                                   40003 [2] alarm.hyst  0.1°C
//                                   40004 [3] alarm.latch 0/1
//                                   40005 [4] temp.period_ms
//                                   40006 [5] temp.offset 0.1°C
//                                   40007 [6] comm.console  (đổi giao thức từ xa!)
//                                   40008 [7] comm.aux
const modbus::Map& modbus_map();

}  // namespace product
