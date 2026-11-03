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
    CHECK(core::is_ok(t.start(osal::ThreadConfig{"worker"}, &osal::Thread::invoke<Worker, &Worker::run>, &w)));
    for (int i = 1; i <= 100; ++i) q.send(i);
    q.send(-1);   // poison pill
    t.join();
    CHECK_EQ(w.sum.load(), 5050);
}
