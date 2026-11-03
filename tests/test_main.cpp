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
        c->fn();
        std::printf("[%s] %s\n", test::failures() == before ? " OK " : "FAIL", c->name);
        ++run;
    }
    std::printf("\n%d test(s), %d failed check(s)\n", run, test::failures());
    return test::failures() == 0 ? 0 : 1;
}
