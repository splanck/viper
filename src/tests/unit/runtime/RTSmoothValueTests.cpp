//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTSmoothValueTests.cpp - Unit tests for rt_smoothvalue
//===----------------------------------------------------------------------===//

#include "rt_smoothvalue.h"
#include <cassert>
#include <cstdio>
#include <cmath>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("  %s...", #name); \
    test_##name(); \
    printf(" OK\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabs((a) - (b)) < (eps))

TEST(create_destroy) {
    rt_smoothvalue sv = rt_smoothvalue_new(100.0, 0.9);
    ASSERT(sv != NULL);
    ASSERT_NEAR(rt_smoothvalue_get(sv), 100.0, 0.001);
    ASSERT_NEAR(rt_smoothvalue_target(sv), 100.0, 0.001);
    rt_smoothvalue_destroy(sv);
}

TEST(set_target) {
    rt_smoothvalue sv = rt_smoothvalue_new(0.0, 0.5);
    rt_smoothvalue_set_target(sv, 100.0);

    ASSERT_NEAR(rt_smoothvalue_target(sv), 100.0, 0.001);
    ASSERT_NEAR(rt_smoothvalue_get(sv), 0.0, 0.001);

    // Update should move toward target
    rt_smoothvalue_update(sv);
    ASSERT(rt_smoothvalue_get(sv) > 0.0);
    ASSERT(rt_smoothvalue_get(sv) < 100.0);
    rt_smoothvalue_destroy(sv);
}

TEST(smoothing_factor) {
    // Low smoothing = fast response
    rt_smoothvalue fast = rt_smoothvalue_new(0.0, 0.1);
    rt_smoothvalue_set_target(fast, 100.0);

    // High smoothing = slow response
    rt_smoothvalue slow = rt_smoothvalue_new(0.0, 0.95);
    rt_smoothvalue_set_target(slow, 100.0);

    rt_smoothvalue_update(fast);
    rt_smoothvalue_update(slow);

    ASSERT(rt_smoothvalue_get(fast) > rt_smoothvalue_get(slow));
    rt_smoothvalue_destroy(fast);
    rt_smoothvalue_destroy(slow);
}

TEST(set_immediate) {
    rt_smoothvalue sv = rt_smoothvalue_new(0.0, 0.9);
    rt_smoothvalue_set_immediate(sv, 50.0);

    ASSERT_NEAR(rt_smoothvalue_get(sv), 50.0, 0.001);
    ASSERT_NEAR(rt_smoothvalue_target(sv), 50.0, 0.001);
    ASSERT(rt_smoothvalue_at_target(sv) == 1);
    rt_smoothvalue_destroy(sv);
}

TEST(impulse) {
    rt_smoothvalue sv = rt_smoothvalue_new(100.0, 0.9);
    rt_smoothvalue_impulse(sv, 20.0);

    ASSERT_NEAR(rt_smoothvalue_get(sv), 120.0, 0.001);
    ASSERT_NEAR(rt_smoothvalue_target(sv), 100.0, 0.001);
    rt_smoothvalue_destroy(sv);
}

TEST(at_target) {
    rt_smoothvalue sv = rt_smoothvalue_new(100.0, 0.9);
    ASSERT(rt_smoothvalue_at_target(sv) == 1);

    rt_smoothvalue_set_target(sv, 200.0);
    ASSERT(rt_smoothvalue_at_target(sv) == 0);

    // Run until converged (smoothing 0.9 needs many iterations)
    for (int i = 0; i < 200; i++) {
        rt_smoothvalue_update(sv);
    }
    ASSERT(rt_smoothvalue_at_target(sv) == 1);
    rt_smoothvalue_destroy(sv);
}

TEST(value_i64) {
    rt_smoothvalue sv = rt_smoothvalue_new(42.7, 0.9);
    ASSERT(rt_smoothvalue_get_i64(sv) == 43);  // Rounded
    rt_smoothvalue_destroy(sv);
}

TEST(velocity) {
    rt_smoothvalue sv = rt_smoothvalue_new(0.0, 0.5);
    rt_smoothvalue_set_target(sv, 100.0);

    ASSERT_NEAR(rt_smoothvalue_velocity(sv), 0.0, 0.001);
    rt_smoothvalue_update(sv);
    ASSERT(rt_smoothvalue_velocity(sv) > 0.0);
    rt_smoothvalue_destroy(sv);
}

int main() {
    printf("RTSmoothValueTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(set_target);
    RUN_TEST(smoothing_factor);
    RUN_TEST(set_immediate);
    RUN_TEST(impulse);
    RUN_TEST(at_target);
    RUN_TEST(value_i64);
    RUN_TEST(velocity);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
