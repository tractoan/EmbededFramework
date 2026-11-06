#pragma once
// Interface thiết bị cho nhóm cổng số (DI/DO công nghiệp, relay, opto...). Bit i = kênh i.
#include <array>
#include <atomic>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "hal/gpio.hpp"
#include "osal/mutex.hpp"

namespace device {

class IDigitalInputs {
public:
    static constexpr std::size_t kMaxChannels = 32;
    virtual ~IDigitalInputs() = default;
    virtual std::size_t count() const = 0;
    virtual std::uint32_t read()      = 0;   // trạng thái tức thời (thread-safe)
};

class IDigitalOutputs {
public:
    static constexpr std::size_t kMaxChannels = 32;
    virtual ~IDigitalOutputs() = default;
    virtual std::size_t count() const = 0;
    // Chỉ các bit trong mask được ghi: out = (out & ~mask) | (value & mask). Thread-safe.
    virtual core::Status write(std::uint32_t mask, std::uint32_t value) = 0;
    virtual std::uint32_t state() const                                  = 0;   // giá trị đã ghi
};

// Hiện thực trên các chân hal::IGpio rời rạc (board cung cấp theo chỉ số).
class GpioInputBank final : public IDigitalInputs, private core::NonCopyable {
public:
    // invert_mask: bit = 1 -> kênh tích cực mức thấp (opto kéo xuống...).
    core::Status add(hal::IGpio* pin);
    void set_invert_mask(std::uint32_t m) { invert_ = m; }
    std::size_t count() const override { return count_; }
    std::uint32_t read() override;

private:
    std::array<hal::IGpio*, kMaxChannels> pins_{};
    std::size_t count_   = 0;
    std::uint32_t invert_ = 0;
};

class GpioOutputBank final : public IDigitalOutputs, private core::NonCopyable {
public:
    core::Status add(hal::IGpio* pin);
    std::size_t count() const override { return count_; }
    core::Status write(std::uint32_t mask, std::uint32_t value) override;
    std::uint32_t state() const override { return state_.load(); }

private:
    std::array<hal::IGpio*, kMaxChannels> pins_{};
    std::size_t count_ = 0;
    std::atomic<std::uint32_t> state_{0};
    osal::Mutex mtx_;
};

}  // namespace device
