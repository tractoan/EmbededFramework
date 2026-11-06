#pragma once
#include "mavlink/mavlink_map.hpp"

namespace product {

// Ánh xạ MAVLink của thermo_pro (sysid 1, compid 191 = MAV_COMP_ID_ONBOARD_COMPUTER... dải người dùng):
//   NAMED_VALUE_FLOAT "temp"            <- kTemperature (°C)
//   STATUSTEXT                          <- kAlarmStateChanged (mức nghiêm trọng theo cảnh báo)
//   HEARTBEAT.system_status             <- kAlarmStateChanged: NORMAL/WARNING = ACTIVE, ALARM/FAULT = CRITICAL
//   COMMAND_LONG MAV_CMD_USER_1         -> xác nhận cảnh báo
//   COMMAND_LONG MAV_CMD_PREFLIGHT_STORAGE (param1 = 1) -> lưu tham số xuống Flash
//   Param protocol                      <-> mọi tham số ParamStore (alarm.*, temp.*, comm.*...)
const mavlink::Map& mavlink_map();

}  // namespace product
