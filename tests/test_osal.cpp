#include <atomic>

#include "osal/osal.hpp"
#include "test_framework.hpp"

TEST(osal_queue_fifo_and_timeout) {
    osal::Queue<int, 3> q;
    CHECK(q.send(1, 0));
    CHECK(q.send(2, 0));
    CHECK(q.send(3, 0));
    CHECK(!q.send(4, 0));   // đầy
    int v = 0;
    CHECK(q.receive(v, 0) && v == 1);
    CHECK(q.receive(v, 0) && v == 2);
    CHECK(q.receive(v, 0) && v == 3);

    const auto t0 = osal::now_ms();
    CHECK(!q.receive(v, 30));   // rỗng -> timeout
    CHECK(osal::now_ms() - t0 >= 25);
}

namespace {
struct Worker {
    osal::Queue<int, 4>* q;
    std::atomic<int> sum{0};
    void run() {
        int v = 0;
        while (q->receive(v) && v >= 0) sum += v;
    }
};
}  // namespace

TEST(osal_thread_consumes_queue) {
    osal::Queue<int, 4> q;
    Worker w{&q};
    osal::Thread t;
    const bool started = core::is_ok(t.start(osal::ThreadConfig{"worker"}, &osal::Thread::invoke<Worker, &Worker::run>, &w));
    CHECK(started);
    if (!started) return;   // không có consumer: send() sẽ chặn mãi
    for (int i = 1; i <= 100; ++i) q.send(i);
    q.send(-1);   // poison pill
    t.join();
    CHECK_EQ(w.sum.load(), 5050);
}

// ---- Ngữ nghĩa mà MỌI port OSAL phải giữ (posix, freertos, ucos2, ucos3...) ----

TEST(osal_semaphore_respects_max_and_timeouts) {
    osal::Semaphore s(0, 2);
    s.give();
    s.give();
    s.give();   // vượt max -> bị bỏ qua
    CHECK(s.take(osal::kNoWait));
    CHECK(s.take(osal::kNoWait));
    CHECK(!s.take(osal::kNoWait));   // kNoWait không được chặn (uC/OS: timeout 0 = chờ mãi!)
    const auto t0 = osal::now_ms();
    CHECK(!s.take(30));
    const auto waited = osal::now_ms() - t0;
    CHECK(waited >= 25 && waited < 1000);
}

namespace {
struct LockProbe {
    osal::Mutex* m;
    std::atomic<int> immediate{-1}, timed{-1};
    void run() {
        immediate = m->try_lock(osal::kNoWait) ? 1 : 0;
        timed     = m->try_lock(30) ? 1 : 0;
    }
};
}  // namespace

TEST(osal_mutex_try_lock_from_other_thread) {
    osal::Mutex m;
    m.lock();
    LockProbe p{&m};
    osal::Thread t;
    CHECK(core::is_ok(t.start(osal::ThreadConfig{"probe"}, &osal::Thread::invoke<LockProbe, &LockProbe::run>, &p)));
    t.join();
    CHECK_EQ(p.immediate.load(), 0);
    CHECK_EQ(p.timed.load(), 0);
    m.unlock();
    CHECK(m.try_lock(osal::kNoWait));
    m.unlock();
}

namespace {
alignas(8) std::uint8_t g_worker_stack[4096];
void bump(void* counter) { static_cast<std::atomic<int>*>(counter)->fetch_add(1); }
}  // namespace

TEST(osal_thread_restart_reuses_resources) {
    // Tạo/hủy lặp lại nhiều hơn số priority trong một dải (uC/OS-II) với cùng một stack tĩnh:
    // port phải trả lại priority/TCB khi join.
    std::atomic<int> runs{0};
    for (int i = 0; i < 12; ++i) {
        osal::Thread t;
        CHECK(core::is_ok(t.start(osal::ThreadConfig{"worker", osal::Priority::Normal, sizeof(g_worker_stack),
                                                     g_worker_stack},
                                  &bump, &runs)));
        t.join();
    }
    CHECK_EQ(runs.load(), 12);
}

TEST(osal_critical_section_nests) {
    int x = 0;
    {
        osal::CriticalSection outer;
        {
            osal::CriticalSection inner;
            x = 1;
        }
        x += 1;
    }
    CHECK_EQ(x, 2);
}
