#include "osal/memory.hpp"
#include "test_framework.hpp"

namespace test {
Case*& head() {
    static Case* h = nullptr;
    return h;
}
int& failures() {
    static int f = 0;
    return f;
}
}  // namespace test

int main() {
    int run = 0;
    for (test::Case* c = test::head(); c != nullptr; c = c->next) {
        const int before = test::failures();
        const std::size_t pool_mark = osal::stack_pool_stats().used;
        c->fn();
        std::fflush(stdout);
        // Test độc lập với kích thước pool của sản phẩm (vd. thermo_lite chỉ có 8 KB).
        if (osal::thread_count() == 0) osal::release_stack_pool_for_test(pool_mark);
        std::printf("[%s] %s\n", test::failures() == before ? " OK " : "FAIL", c->name);
        ++run;
    }
    std::printf("\n%d test(s), %d failed check(s)\n", run, test::failures());
    return test::failures() == 0 ? 0 : 1;
}
