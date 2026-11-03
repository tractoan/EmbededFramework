// Ánh xạ mô hình dữ liệu chuẩn (Event / Param / hành động) -> Modbus.
// Đây là "cột Modbus" của sản phẩm: thêm/bớt thanh ghi chỉ là sửa các bảng dưới đây.
#include "modbus_map.hpp"

#include "app_events.hpp"
#include "app_params.hpp"
#include "comm/link_manager.hpp"

namespace product {

namespace {

bool temperature(const svc::Event& e, std::int32_t& v) {
    app::events::TemperatureSample s{};
    if (!e.get(s)) return false;
    v = s.deci_celsius;
    return true;
}

bool alarm_level(const svc::Event& e, std::int32_t& v) {
    app::events::AlarmStateChanged a{};
    if (!e.get(a)) return false;
    v = static_cast<std::int32_t>(a.to);
    return true;
}

core::Status ack_alarm(comm::Services& s, bool on) {
    if (on) s.bus.publish(svc::Event::make(app::events::kAlarmAckRequest));
    return core::Status::Ok;
}

core::Status save_params(comm::Services& s, bool on) { return on ? s.params.save() : core::Status::Ok; }

const modbus::InputRegister kInputs[] = {
    {0, app::events::kTemperature, &temperature, true},
    {1, app::events::kAlarmStateChanged, &alarm_level, false},
};

const modbus::HoldingRegister kHoldings[] = {
    {0, app::params::kAlarmWarnDeci, true},
    {1, app::params::kAlarmHighDeci, true},
    {2, app::params::kAlarmHysteresisDeci, false},
    {3, app::params::kAlarmLatch, false},
    {4, app::params::kTempSamplePeriodMs, false},
    {5, app::params::kTempOffsetDeci, true},
    {6, comm::LinkManager::kParamBase + 0, false},   // comm.console
    {7, comm::LinkManager::kParamBase + 1, false},   // comm.aux
};

const modbus::Coil kCoils[] = {
    {0, &ack_alarm},
    {1, &save_params},
};

constexpr modbus::Map kMap = modbus::make_map(1, kInputs, kHoldings, kCoils);

}  // namespace

const modbus::Map& modbus_map() { return kMap; }

}  // namespace product
