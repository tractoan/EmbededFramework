#pragma once
// Bảng ID tham số tập trung của tầng nghiệp vụ – mỗi module một dải 0x100.
#include "services/param_store.hpp"

namespace app::params {

// TemperatureMonitor: 0x1000..0x10FF
inline constexpr svc::ParamId kTempSamplePeriodMs = 0x1001;
inline constexpr svc::ParamId kTempOffsetDeci     = 0x1002;
inline constexpr svc::ParamId kTempFilterWindow   = 0x1003;

// AlarmController: 0x1100..0x11FF
inline constexpr svc::ParamId kAlarmWarnDeci       = 0x1101;
inline constexpr svc::ParamId kAlarmHighDeci       = 0x1102;
inline constexpr svc::ParamId kAlarmHysteresisDeci = 0x1103;
inline constexpr svc::ParamId kAlarmLatch          = 0x1104;

// Module riêng của từng sản phẩm: 0x8000..
inline constexpr svc::ParamId kProductBase = 0x8000;

}  // namespace app::params
