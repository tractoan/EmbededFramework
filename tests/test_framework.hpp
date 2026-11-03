#pragma once
// Micro test framework (không exception, không phụ thuộc ngoài).
#include <cstdio>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
    Case* next;
};

Case*& head();
int& failures();

struct Registrar {
    Case c;
    Registrar(const char* name, void (*fn)()) : c{name, fn, nullptr} {
        c.next = head();
        head() = &c;
    }
};

}  // namespace test

#define TEST(name)                                                       \
    static void name();                                                  \
    static ::test::Registrar name##_registrar(#name, &name);             \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            ++::test::failures();                                                     \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
