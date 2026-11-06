#pragma once
// Mock HAL / device: cho phép test nghiệp vụ & service hoàn toàn không cần phần cứng.
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "comm/byte_stream.hpp"
#include "utils/crc.hpp"

#include "hal/gpio.hpp"
#include "hal/storage.hpp"
#include "device/temperature_sensor.hpp"
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

class TemperatureSensor final : public device::ITemperatureSensor {
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

// Stream giả lập kênh truyền: test "gửi" byte vào (feed), đọc lại byte thiết bị trả ra (tx).
class ScriptedStream final : public comm::IByteStream {
public:
    void feed(const std::vector<std::uint8_t>& bytes) {
        std::lock_guard<std::mutex> g(m_);
        rx_.insert(rx_.end(), bytes.begin(), bytes.end());
    }
    void feed(const std::string& text) { feed(std::vector<std::uint8_t>(text.begin(), text.end())); }
    std::vector<std::uint8_t> tx() const {
        std::lock_guard<std::mutex> g(m_);
        return tx_;
    }
    std::string tx_text() const {
        auto t = tx();
        return std::string(t.begin(), t.end());
    }
    void clear_tx() {
        std::lock_guard<std::mutex> g(m_);
        tx_.clear();
    }

    std::size_t write(const std::uint8_t* d, std::size_t n, std::uint32_t) override {
        std::lock_guard<std::mutex> g(m_);
        tx_.insert(tx_.end(), d, d + n);
        return n;
    }
    std::size_t read(std::uint8_t* d, std::size_t n, std::uint32_t timeout_ms) override {
        {
            std::lock_guard<std::mutex> g(m_);
            std::size_t k = 0;
            while (k < n && !rx_.empty()) {
                d[k++] = rx_.front();
                rx_.pop_front();
            }
            if (k > 0) return k;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms < 5 ? timeout_ms : 5));
        return 0;
    }

private:
    mutable std::mutex m_;
    std::deque<std::uint8_t> rx_;
    std::vector<std::uint8_t> tx_;
};

// Chờ điều kiện (tối đa timeout) – cho test có thread.
template <typename Pred>
bool wait_until(Pred pred, int timeout_ms = 1000) {
    for (int t = 0; t < timeout_ms; t += 5) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

// Tạo khung Modbus RTU: thêm CRC (byte thấp trước).
inline std::vector<std::uint8_t> rtu(std::vector<std::uint8_t> pdu) {
    const std::uint16_t crc = utils::crc16_modbus(pdu.data(), pdu.size());
    pdu.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    pdu.push_back(static_cast<std::uint8_t>(crc >> 8));
    return pdu;
}

}  // namespace mock
