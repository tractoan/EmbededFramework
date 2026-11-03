#pragma once
// Mock HAL / device: cho phép test nghiệp vụ & service hoàn toàn không cần phần cứng.
#include <array>
#include <cstring>
#include <vector>

#include "hal/gpio.hpp"
#include "hal/storage.hpp"
#include "services/drivers/temperature_sensor.hpp"
#include "services/event.hpp"

namespace mock {

class Gpio final : public hal::IGpio {
public:
    core::Status configure(hal::PinMode) override { return core::Status::Ok; }
    core::Status write(bool level) override {
        level_ = level;
        ++writes;
        return core::Status::Ok;
    }
    bool read() const override { return level_; }
    int writes = 0;

private:
    bool level_ = false;
};

class Storage final : public hal::INvStorage {
public:
    Storage() { mem.fill(0xFF); }
    std::size_t size() const override { return mem.size(); }
    core::Status read(std::size_t off, void* d, std::size_t n) override {
        if (off + n > mem.size()) return core::Status::OutOfRange;
        std::memcpy(d, &mem[off], n);
        return core::Status::Ok;
    }
    core::Status write(std::size_t off, const void* d, std::size_t n) override {
        if (off + n > mem.size()) return core::Status::OutOfRange;
        std::memcpy(&mem[off], d, n);
        return core::Status::Ok;
    }
    core::Status erase() override {
        mem.fill(0xFF);
        return core::Status::Ok;
    }
    std::array<std::uint8_t, 1024> mem{};
};

class TemperatureSensor final : public svc::ITemperatureSensor {
public:
    core::Status read_deci_celsius(std::int32_t& out) override {
        if (fail) return core::Status::Timeout;
        out = value;
        return core::Status::Ok;
    }
    std::int32_t value = 250;
    bool fail = false;
};

// Ghi lại mọi event đi qua bus.
class Recorder final : public svc::IEventListener {
public:
    void on_event(const svc::Event& e) override { events.push_back(e); }
    std::size_t count(svc::EventId id) const {
        std::size_t n = 0;
        for (const auto& e : events) n += (e.id == id) ? 1 : 0;
        return n;
    }
    std::vector<svc::Event> events;
};

}  // namespace mock
