// Thread riêng của module (app::ModuleWorker) dưới ModuleManager thật (thread thật).
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "app/module_manager.hpp"
#include "messaging/buffer_allocators.hpp"
#include "osal/memory.hpp"
#include "osal/time.hpp"
#include "test_framework.hpp"

namespace {
constexpr msg::EventId kJob = 0x2300;

struct Ctx {
    msg::EventBus bus;
    svc::ParamStore params;
    svc::CommandService cli;
    svc::LedService leds;
    app::Devices devices;
    app::SystemContext ctx{bus, params, cli, leds, devices};
};

template <typename Pred>
bool wait_until(Pred p, int timeout_ms = 2000) {
    for (int i = 0; i < timeout_ms; ++i) {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return p();
}

const osal::ThreadConfig kApp{"app", osal::Priority::Normal, 4096, nullptr};

class WorkerModule final : public app::IModule, private app::IWorkerHandler {
public:
    explicit WorkerModule(std::uint32_t period = 0) : period_(period) {}
    const char* name() const override { return "worker_mod"; }
    core::Status on_init(app::SystemContext&) override { return core::Status::Ok; }
    app::ModuleWorkerBase* worker() override { return &worker_; }

    void on_event(const msg::Event& e) override {   // thread ứng dụng: chỉ chuyển việc
        if (e.id != kJob) return;
        app_thread_ = std::this_thread::get_id();
        worker_.post(e);
    }
    void on_stop() override {
        stopped_with_worker_running_ = worker_.running();
        stopped_ = true;
    }

    // ---- thread worker
    void on_job(const msg::Event& job) override {
        if (stopped_) job_after_stop_ = true;
        worker_thread_ = std::this_thread::get_id();
        last_data_     = job.buffer.data();
        in_job_        = true;
        while (block_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        in_job_ = false;
        ++jobs_;
    }
    std::uint32_t worker_period_ms() const override { return period_.load(); }
    void on_worker_tick(std::uint64_t now_ms) override {
        if (first_tick_ == 0) first_tick_ = now_ms;
        last_tick_ = now_ms;
        ++ticks_;
        if (tick_work_ms_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(tick_work_ms_));
    }

    app::ModuleWorker<2> worker_{*this, {"wk", osal::Priority::Normal, 4096, nullptr}};
    std::atomic<std::uint32_t> period_;
    std::atomic<int> jobs_{0}, ticks_{0};
    std::atomic<bool> block_{false}, in_job_{false}, stopped_{false}, job_after_stop_{false};
    bool stopped_with_worker_running_ = true;
    std::thread::id app_thread_{}, worker_thread_{};
    std::atomic<const std::uint8_t*> last_data_{nullptr};
    std::uint64_t first_tick_ = 0, last_tick_ = 0;
    int tick_work_ms_ = 0;
};

class PlainModule final : public app::IModule {
public:
    const char* name() const override { return "plain"; }
    core::Status on_init(app::SystemContext&) override { return core::Status::Ok; }
};
}  // namespace

TEST(module_worker_runs_jobs_on_own_thread_zero_copy) {
    static msg::StaticBlockPool<64, 2> blocks;
    static msg::PoolAllocator pool({&blocks});
    Ctx c;
    WorkerModule m;
    app::ModuleManager mm;
    CHECK(core::is_ok(mm.add(&m)));
    CHECK(core::is_ok(mm.init_all(c.ctx)));
    CHECK(core::is_ok(mm.start(kApp)));
    CHECK(m.worker_.running());

    msg::Buffer data = msg::Buffer::allocate(pool, 32);
    const std::uint8_t* original = data.data();
    c.bus.publish(msg::Event::make(kJob, std::move(data)));
    CHECK(wait_until([&] { return m.jobs_.load() == 1; }));
    CHECK(m.last_data_.load() == original);              // cùng vùng nhớ: Buffer chỉ tăng tham chiếu
    CHECK(m.app_thread_ != std::thread::id{} && m.worker_thread_ != std::thread::id{});
    CHECK(m.app_thread_ != m.worker_thread_);              // on_event và on_job ở hai thread khác nhau
    CHECK(wait_until([&] { return pool.pool_stats(0).used == 0; }));   // worker xong -> block trả về pool
    CHECK_EQ(m.worker_.processed(), 1u);

    mm.stop();
    CHECK(!m.worker_.running());
    CHECK(m.stopped_.load() && !m.stopped_with_worker_running_);   // on_stop chạy sau khi worker đã dừng
    CHECK(!m.job_after_stop_.load());
}

TEST(module_worker_queue_full_counts_dropped) {
    Ctx c;
    WorkerModule m;
    app::ModuleManager mm;
    mm.add(&m);
    mm.init_all(c.ctx);
    CHECK(core::is_ok(mm.start(kApp)));

    m.block_ = true;
    CHECK(m.worker_.post(msg::Event::make(kJob)));        // worker lấy ra và bị chặn trong on_job
    CHECK(wait_until([&] { return m.in_job_.load(); }));
    CHECK(m.worker_.post(msg::Event::make(kJob)));        // hàng đợi sâu 2
    CHECK(m.worker_.post(msg::Event::make(kJob)));
    CHECK(!m.worker_.post(msg::Event::make(kJob)));       // đầy, không chờ
    CHECK(!m.worker_.post(msg::Event::make(kJob), 20));   // đầy, chờ 20 ms vẫn đầy
    CHECK_EQ(m.worker_.dropped(), 2u);
    m.block_ = false;
    CHECK(wait_until([&] { return m.jobs_.load() == 3; }));
    mm.stop();
}

TEST(module_worker_tick_does_not_drift) {
    Ctx c;
    WorkerModule m(20);
    m.tick_work_ms_ = 5;   // mỗi tick tốn 5 ms: lịch kiểu "now + period" sẽ ra chu kỳ ~25 ms
    app::ModuleManager mm;
    mm.add(&m);
    mm.init_all(c.ctx);
    CHECK(core::is_ok(mm.start(kApp)));
    CHECK(wait_until([&] { return m.ticks_.load() >= 21; }, 3000));
    mm.stop();
    const int n           = m.ticks_.load();
    const double interval = static_cast<double>(m.last_tick_ - m.first_tick_) / (n - 1);
    std::printf("    worker tick: %d ticks, mean interval %.2f ms (period 20 ms, 5 ms work)\n", n, interval);
    CHECK(interval >= 19.0 && interval <= 21.5);
}

TEST(module_worker_period_change_at_runtime) {
    Ctx c;
    WorkerModule m(0);
    app::ModuleManager mm;
    mm.add(&m);
    mm.init_all(c.ctx);
    CHECK(core::is_ok(mm.start(kApp)));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK_EQ(m.ticks_.load(), 0);                          // period 0: không tick
    m.period_ = 10;                                        // bật lúc chạy
    CHECK(wait_until([&] { return m.ticks_.load() >= 5; }));
    m.period_ = 0;                                         // tắt lúc chạy
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const int frozen = m.ticks_.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK_EQ(m.ticks_.load(), frozen);
    mm.stop();
}

TEST(module_worker_start_failure_and_config_override) {
    Ctx c;
    // Pool stack là bộ cấp phát tăng dần: trả về mốc sau mỗi khối (mọi thread đã join) để các khối
    // không cộng dồn – sản phẩm nhỏ (thermo_lite: pool 8 KB) vẫn đủ chỗ.
    const std::size_t pool_mark = osal::stack_pool_stats().used;
    {   // module không có worker mà sản phẩm lại cấu hình worker -> lỗi cấu hình
        PlainModule plain;
        app::ModuleManager mm;
        const osal::ThreadConfig cfg{"x", osal::Priority::Low, 1024, nullptr};
        CHECK(mm.add(&plain, &cfg) == core::Status::InvalidArg);
    }
    {   // stack của worker vượt pool -> start lỗi, không để lại thread nào, stop() vẫn sạch
        WorkerModule m;
        app::ModuleManager mm;
        const osal::ThreadConfig huge{"huge", osal::Priority::Low, osal::stack_pool_stats().capacity * 2, nullptr};
        CHECK(core::is_ok(mm.add(&m, &huge)));
        mm.init_all(c.ctx);
        const std::size_t threads_before = osal::thread_count();
        CHECK(!core::is_ok(mm.start(kApp)));
        CHECK(!m.worker_.running());
        CHECK_EQ(osal::thread_count(), threads_before);
        mm.stop();
        CHECK(m.stopped_.load());
    }
    osal::release_stack_pool_for_test(pool_mark);
    {   // worker thứ nhất đã chạy, worker thứ hai lỗi -> worker thứ nhất phải được dừng lại
        WorkerModule ok_mod, bad_mod;
        app::ModuleManager mm;
        const osal::ThreadConfig huge{"huge", osal::Priority::Low, osal::stack_pool_stats().capacity * 2, nullptr};
        mm.add(&ok_mod);
        mm.add(&bad_mod, &huge);
        mm.init_all(c.ctx);
        const std::size_t threads_before = osal::thread_count();
        CHECK(!core::is_ok(mm.start(kApp)));
        CHECK(!ok_mod.worker_.running());
        CHECK_EQ(osal::thread_count(), threads_before);
        mm.stop();
    }
    osal::release_stack_pool_for_test(pool_mark);
    {   // sản phẩm ghi đè priority/stack của worker
        WorkerModule m;
        app::ModuleManager mm;
        const osal::ThreadConfig cfg{"custom", osal::Priority::High, 3072, nullptr};
        mm.add(&m, &cfg);
        mm.init_all(c.ctx);
        CHECK(core::is_ok(mm.start(kApp)));
        CHECK(std::string(m.worker_.config().name) == "custom");
        CHECK(m.worker_.config().priority.level() == osal::Priority::High.level());
        CHECK_EQ(m.worker_.config().stack_bytes, 3072u);
        mm.stop();
    }
}

TEST(module_worker_no_heap_after_startup) {
    static std::atomic<int> violations{0};
    Ctx c;
    WorkerModule m(5);
    app::ModuleManager mm;
    mm.add(&m);
    mm.init_all(c.ctx);
    CHECK(core::is_ok(mm.start(kApp)));
    osal::set_heap_violation_handler([](std::size_t) { violations.fetch_add(1); });
    osal::end_startup();   // từ đây: post, xử lý việc và tick không được cấp phát
    for (int i = 0; i < 20; ++i) {
        c.bus.publish(msg::Event::make(kJob));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(wait_until([&] { return m.worker_.processed() + m.worker_.dropped() == 20 && m.ticks_.load() >= 5; }));
    osal::reset_startup_for_test();
    osal::set_heap_violation_handler(nullptr);
    mm.stop();
    CHECK_EQ(violations.load(), 0);
}
