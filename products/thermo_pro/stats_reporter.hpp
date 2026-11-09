#pragma once
// Module nghiệp vụ RIÊNG của sản phẩm thermo_pro (không nằm trong thư viện dùng chung):
// thống kê min/avg/max nhiệt độ và số lần cảnh báo theo chu kỳ cấu hình được.
// Tiêu thụ kTemperatureBatch: đọc thẳng mảng mẫu trong Event::buffer (không copy dữ liệu).
#include <atomic>
#include <climits>
#include <cstring>

#include "app/module.hpp"
#include "app_events.hpp"
#include "app_params.hpp"
#include "services/logger.hpp"
#include "utils/fixed_point.hpp"

namespace product {

// Ví dụ module có thread riêng (app::ModuleWorker): thread ứng dụng chỉ chuyển lô mẫu sang worker
// (Buffer đếm tham chiếu – không copy dữ liệu); worker sở hữu toàn bộ số liệu thống kê, tính toán
// và ghi log báo cáo theo chu kỳ không trôi – không làm chậm các module khác.
class StatsReporter final : public app::IModule, private app::IWorkerHandler {
public:
    static constexpr svc::ParamId kPeriodParam = app::params::kProductBase + 1;

    const char* name() const override { return "stats_reporter"; }

    core::Status on_init(app::SystemContext& ctx) override {
        ctx_ = &ctx;
        return ctx.params.register_params(&def_, 1);
    }
    core::Status on_start() override {
        period_ = static_cast<std::uint32_t>(ctx_->params.get_or(kPeriodParam, def_.default_value));
        return core::Status::Ok;
    }
    app::ModuleWorkerBase* worker() override { return &worker_; }

    // ---- thread ứng dụng: không giữ số liệu, chỉ chuyển việc
    void on_event(const msg::Event& e) override {
        svc::ParamChanged pc{};
        if (e.id == app::events::kTemperatureBatch || e.id == app::events::kAlarmStateChanged) {
            if (!worker_.post(e) && !drop_warned_) {   // hàng đợi đầy: worker không theo kịp
                LOGW("stats", "worker queue full - batch dropped");
                drop_warned_ = true;
            }
        } else if (e.id == svc::evt::kParamChanged && e.get(pc) && pc.id == kPeriodParam) {
            period_ = static_cast<std::uint32_t>(pc.value);   // worker đọc lại ở vòng kế tiếp
        }
    }

private:
    // ---- thread worker: sở hữu min_/max_/sum_/n_... (thread ứng dụng không chạm tới)
    void on_job(const msg::Event& e) override {
        app::events::TemperatureBatch batch{};
        if (e.id == app::events::kTemperatureBatch && e.get(batch)) {
            const std::size_t n = e.buffer.size() / sizeof(std::int16_t);
            for (std::size_t i = 0; i < n && i < batch.count; ++i) {
                std::int16_t v = 0;
                std::memcpy(&v, e.buffer.data() + i * sizeof(v), sizeof(v));
                min_ = v < min_ ? v : min_;
                max_ = v > max_ ? v : max_;
                sum_ += v;
                ++n_;
            }
            ++batches_;
            bytes_ += e.buffer.size();
        } else if (e.id == app::events::kAlarmStateChanged) {
            ++transitions_;
        }
    }

    std::uint32_t worker_period_ms() const override { return period_.load(); }

    void on_worker_tick(std::uint64_t /*now_ms*/) override {
        if (n_ == 0) return;
        LOGI("stats", "T min/avg/max = %s / %s / %s C over %lu samples (%lu batches, %lu B), %lu alarm transitions",
             utils::Fixed(min_, 1).c_str(), utils::Fixed(static_cast<std::int32_t>(sum_ / n_), 1).c_str(),
             utils::Fixed(max_, 1).c_str(), static_cast<unsigned long>(n_), static_cast<unsigned long>(batches_),
             static_cast<unsigned long>(bytes_), static_cast<unsigned long>(transitions_));
        min_ = INT32_MAX;
        max_ = INT32_MIN;
        sum_ = 0;
        n_ = transitions_ = batches_ = 0;
        bytes_ = 0;
    }

    const svc::ParamDef def_{kPeriodParam, "stats.period_ms", 10000, 1000, 3600000, "ms"};
    app::SystemContext* ctx_ = nullptr;
    std::atomic<std::uint32_t> period_{10000};   // ghi: thread ứng dụng, đọc: worker
    bool drop_warned_ = false;
    std::int32_t min_ = INT32_MAX, max_ = INT32_MIN;
    std::int64_t sum_ = 0;
    std::uint32_t n_ = 0, transitions_ = 0, batches_ = 0;
    std::size_t bytes_ = 0;
    // Khai báo CUỐI: hủy TRƯỚC các thành viên mà thread worker dùng (dừng thread rồi mới hủy dữ liệu).
    // Hàng đợi 4 lô; priority thấp: thống kê không gấp. Sản phẩm có thể ghi đè khi add().
    app::ModuleWorker<4> worker_{*this, {"stats", osal::Priority::Low, 2048, nullptr}};
};

}  // namespace product
