#pragma once
#include <array>
#include <atomic>

#include "app/module.hpp"
#include "app_events.hpp"

namespace app {

// Nghiệp vụ: lấy mẫu nhiệt độ định kỳ, hiệu chỉnh offset, lọc trung bình trượt,
// phát hiện lỗi cảm biến -> phát event cho các module khác.
class TemperatureMonitor final : public IModule, public svc::ICommandHandler {
public:
    struct Config {   // cấu hình mặc định do SẢN PHẨM chọn (compile-time)
        std::int32_t sample_period_ms = 1000;
        std::int32_t filter_window    = 4;    // 1..8
        std::uint8_t fault_threshold  = 3;    // số lần đọc lỗi liên tiếp -> Fault
    };

    explicit TemperatureMonitor(const Config& cfg);

    const char* name() const override { return "temp_monitor"; }
    core::Status on_init(SystemContext& ctx) override;
    core::Status on_start() override;
    void on_event(const svc::Event& e) override;
    std::uint32_t tick_period_ms() const override { return period_ms_; }
    void on_tick(std::uint64_t now_ms) override;

    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "temp"

private:
    static constexpr std::size_t kMaxWindow = 8;
    void apply_params();
    std::int32_t filter(std::int32_t sample);

    Config cfg_;
    std::array<svc::ParamDef, 3> defs_;
    SystemContext* ctx_ = nullptr;

    std::uint32_t period_ms_ = 1000;
    std::int32_t offset_     = 0;
    std::size_t window_      = 4;
    std::array<std::int32_t, kMaxWindow> history_{};
    std::size_t hist_count_ = 0;
    std::size_t hist_pos_   = 0;
    std::uint8_t consecutive_errors_ = 0;
    bool faulted_ = false;

    // Đọc từ thread CLI -> atomic
    std::atomic<std::int32_t> last_value_{0};
    std::atomic<bool> has_value_{false};
    std::atomic<std::uint32_t> samples_{0};
    std::atomic<std::uint32_t> errors_{0};
};

}  // namespace app
