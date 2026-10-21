#pragma once

#include <cmath>
#include <cstdio>

namespace eee::test {

inline int failures = 0;

inline bool nearlyEqual(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a - b) <= epsilon;
}

inline int report(const char* suite) {
    if (failures == 0) {
        std::printf("%s: all checks passed.\n", suite);
        return 0;
    }
    std::printf("%s: %d check(s) failed.\n", suite, failures);
    return 1;
}

} // namespace eee::test

#define CHECK(condition)                                                                          \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                     \
            ++eee::test::failures;                                                                \
        }                                                                                         \
    } while (false)
