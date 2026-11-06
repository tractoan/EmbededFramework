#pragma once
#include "device/temperature_sensor.hpp"
#include "services/command_service.hpp"
#include "messaging/event_bus.hpp"
#include "services/led_service.hpp"
#include "services/param_store.hpp"

namespace app {

// Các thiết bị đã được khởi tạo (theo interface). nullptr = board không có.
struct Devices {
    device::ITemperatureSensor* temperature = nullptr;
};

// Dependency Injection: mọi thứ module nghiệp vụ được phép dùng.
// Module KHÔNG truy cập trực tiếp HAL / OSAL native / biến toàn cục.
struct SystemContext {
    msg::EventBus& bus;
    svc::ParamStore& params;
    svc::CommandService& cli;
    svc::LedService& leds;
    Devices& devices;
};

}  // namespace app
