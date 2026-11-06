#pragma once
// Bảng ID tham số tập trung của tầng nghiệp vụ – mỗi module một dải 0x100.
#include "services/param_store.hpp"

namespace app::params {

// TemperatureMonitor: 0x1000..0x10FF
inline constexpr svc::ParamId kTempSamplePeriodMs = 0x1001;
inline constexpr svc::ParamId kTempOffsetDeci     = 0x1002;
inline constexpr svc::ParamId kTempFilterWindow   = 0x1003;
inline constexpr svc::ParamId kTempBatchSize      = 0x1004;

// AlarmController: 0x1100..0x11FF
inline constexpr svc::ParamId kAlarmWarnDeci       = 0x1101;
inline constexpr svc::ParamId kAlarmHighDeci       = 0x1102;
inline constexpr svc::ParamId kAlarmHysteresisDeci = 0x1103;
inline constexpr svc::ParamId kAlarmLatch          = 0x1104;

// Bring-up phần cứng (tên <= 16 ký tự để đọc/ghi được qua MAVLink PARAM_*)
//   SerialBringup: 0x1200..0x121F
inline constexpr svc::ParamId kSerPeriodMs   = 0x1201;   // ser.period_ms
inline constexpr svc::ParamId kSerTimeoutMs  = 0x1202;   // ser.timeout_ms
inline constexpr svc::ParamId kSerReportMs   = 0x1203;   // ser.report_ms
inline constexpr svc::ParamId kGnssDetectMs  = 0x1211;   // gnss.detect_ms
inline constexpr svc::ParamId kGnssForward   = 0x1212;   // gnss.forward
inline constexpr svc::ParamId kGnssBaud      = 0x1213;   // gnss.baud
//   IoBringup: 0x1220..0x122F
inline constexpr svc::ParamId kIoReportMs    = 0x1221;   // io.report_ms
//   ImuBringup: 0x1230..0x123F
inline constexpr svc::ParamId kImuRateHz     = 0x1231;   // imu.rate_hz
inline constexpr svc::ParamId kImuTimeoutMs  = 0x1232;   // imu.timeout_ms

// Module riêng của từng sản phẩm: 0x8000..
inline constexpr svc::ParamId kProductBase = 0x8000;

}  // namespace app::params
