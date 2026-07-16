//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTTweenTests.cpp - Unit tests for rt_tween
//===----------------------------------------------------------------------===//

#include "rt_tween.h"
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        printf("  %s...", #name);                                                                  \
        test_##name();                                                                             \
        printf(" OK\n");                                                                           \
        tests_passed++;                                                                            \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf(" FAILED at line %d: %s\n", __LINE__, #cond);                                   \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabs((a) - (b)) < (eps))

TEST(create_destroy) {
    rt_tween tw = rt_tween_new();
    ASSERT(tw != NULL);
    ASSERT(rt_tween_is_running(tw) == 0);
    ASSERT(rt_tween_is_complete(tw) == 0);
    rt_tween_destroy(tw);
}

TEST(start_linear) {
    rt_tween tw = rt_tween_new();
    rt_tween_start(tw, 0.0, 100.0, 10, RT_EASE_LINEAR);

    ASSERT(rt_tween_is_running(tw) == 1);
    ASSERT_NEAR(rt_tween_value(tw), 0.0, 0.1);

    // Update 5 frames (50%)
    for (int i = 0; i < 5; i++)
        rt_tween_update(tw);
    ASSERT_NEAR(rt_tween_value(tw), 50.0, 1.0);
    ASSERT(rt_tween_progress(tw) == 50);

    // Update to completion
    for (int i = 0; i < 5; i++)
        rt_tween_update(tw);
    ASSERT_NEAR(rt_tween_value(tw), 100.0, 0.1);
    ASSERT(rt_tween_is_complete(tw) == 1);
    ASSERT(rt_tween_is_running(tw) == 0);
    rt_tween_destroy(tw);
}

TEST(start_i64) {
    rt_tween tw = rt_tween_new();
    rt_tween_start_i64(tw, 0, 200, 20, RT_EASE_LINEAR);

    for (int i = 0; i < 10; i++)
        rt_tween_update(tw);
    ASSERT(rt_tween_value_i64(tw) == 100);
    rt_tween_destroy(tw);
}

TEST(pause_resume) {
    rt_tween tw = rt_tween_new();
    rt_tween_start(tw, 0.0, 100.0, 10, RT_EASE_LINEAR);

    rt_tween_update(tw);
    rt_tween_update(tw);
    double val1 = rt_tween_value(tw);

    rt_tween_pause(tw);
    ASSERT(rt_tween_is_paused(tw) == 1);
    rt_tween_update(tw);
    rt_tween_update(tw);
    ASSERT_NEAR(rt_tween_value(tw), val1, 0.001);

    rt_tween_resume(tw);
    ASSERT(rt_tween_is_paused(tw) == 0);
    rt_tween_update(tw);
    ASSERT(rt_tween_value(tw) > val1);
    rt_tween_destroy(tw);
}

TEST(pause_only_marks_active_tween) {
    rt_tween tw = rt_tween_new();
    ASSERT(tw != NULL);

    rt_tween_pause(tw);
    ASSERT(rt_tween_is_paused(tw) == 0);

    rt_tween_start(tw, 0.0, 1.0, 1, RT_EASE_LINEAR);
    ASSERT(rt_tween_update(tw) == 1);
    ASSERT(rt_tween_is_complete(tw) == 1);
    rt_tween_pause(tw);
    ASSERT(rt_tween_is_paused(tw) == 0);

    rt_tween_start(tw, 0.0, 1.0, 2, RT_EASE_LINEAR);
    rt_tween_pause(tw);
    ASSERT(rt_tween_is_paused(tw) == 1);

    rt_tween_destroy(tw);
}

TEST(stop_reset) {
    rt_tween tw = rt_tween_new();
    rt_tween_start(tw, 0.0, 100.0, 10, RT_EASE_LINEAR);

    for (int i = 0; i < 5; i++)
        rt_tween_update(tw);
    rt_tween_stop(tw);
    ASSERT(rt_tween_is_running(tw) == 0);

    rt_tween_reset(tw);
    ASSERT(rt_tween_is_running(tw) == 1);
    ASSERT_NEAR(rt_tween_value(tw), 0.0, 0.1);
    rt_tween_destroy(tw);
}

TEST(ease_functions) {
    // Test that different easing types produce different curves
    double t = 0.5;
    double linear = rt_tween_ease(t, RT_EASE_LINEAR);
    double in_quad = rt_tween_ease(t, RT_EASE_IN_QUAD);
    double out_quad = rt_tween_ease(t, RT_EASE_OUT_QUAD);

    ASSERT_NEAR(linear, 0.5, 0.001);
    ASSERT(in_quad < linear);  // ease-in is slower at midpoint
    ASSERT(out_quad > linear); // ease-out is faster at midpoint
}

TEST(lerp_i64) {
    ASSERT(rt_tween_lerp_i64(0, 100, 0.0) == 0);
    ASSERT(rt_tween_lerp_i64(0, 100, 0.5) == 50);
    ASSERT(rt_tween_lerp_i64(0, 100, 1.0) == 100);
    ASSERT(rt_tween_lerp_i64(-100, 100, 0.5) == 0);
}

TEST(nonfinite_inputs_are_sanitized) {
    rt_tween tw = rt_tween_new();
    rt_tween_start(tw, NAN, INFINITY, 2, RT_EASE_LINEAR);
    ASSERT(rt_tween_value(tw) == 0.0);
    rt_tween_update(tw);
    ASSERT(std::isfinite(rt_tween_value(tw)));
    ASSERT(rt_tween_lerp_i64(0, 100, NAN) == 0);
    ASSERT(rt_tween_ease(NAN, RT_EASE_LINEAR) == 0.0);
    rt_tween_destroy(tw);
}

TEST(large_opposite_signed_endpoints_stay_finite) {
    rt_tween tw = rt_tween_new();
    rt_tween_start(tw, -DBL_MAX, DBL_MAX, 2, RT_EASE_LINEAR);
    rt_tween_update(tw);
    ASSERT(std::isfinite(rt_tween_value(tw)));
    ASSERT_NEAR(rt_tween_value(tw), 0.0, 1.0);
    rt_tween_destroy(tw);
}

// VDOC-273: StartI64 / ValueI64 / LerpI64 must preserve integer endpoints exactly,
// including values above 2^53 that binary64 cannot represent. Before the fix the
// endpoints were cast to double on entry and 2^53+1 collapsed to 2^53.
TEST(i64_preserves_values_above_2_53) {
    const int64_t big = 9007199254740993LL; // 2^53 + 1, not representable in double

    // Constant tween: the value must never drift, at any frame.
    rt_tween tw = rt_tween_new();
    rt_tween_start_i64(tw, big, big, 1, RT_EASE_LINEAR);
    ASSERT(rt_tween_value_i64(tw) == big); // before any update
    rt_tween_update(tw);
    ASSERT(rt_tween_value_i64(tw) == big); // after completion
    rt_tween_destroy(tw);

    // A moving tween exposes its exact endpoints at the start and end.
    rt_tween tw2 = rt_tween_new();
    rt_tween_start_i64(tw2, 0, big, 4, RT_EASE_LINEAR);
    ASSERT(rt_tween_value_i64(tw2) == 0); // start exact
    for (int i = 0; i < 4; i++)
        rt_tween_update(tw2);
    ASSERT(rt_tween_is_complete(tw2) == 1);
    ASSERT(rt_tween_value_i64(tw2) == big); // end exact
    rt_tween_destroy(tw2);

    // The static integer lerp preserves endpoints and constants too.
    ASSERT(rt_tween_lerp_i64(big, big, 0.5) == big);
    ASSERT(rt_tween_lerp_i64(0, big, 0.0) == 0);
    ASSERT(rt_tween_lerp_i64(0, big, 1.0) == big);
}

/// @brief Main.
int main() {
    printf("RTTweenTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(start_linear);
    RUN_TEST(start_i64);
    RUN_TEST(pause_resume);
    RUN_TEST(pause_only_marks_active_tween);
    RUN_TEST(stop_reset);
    RUN_TEST(ease_functions);
    RUN_TEST(lerp_i64);
    RUN_TEST(i64_preserves_values_above_2_53);
    RUN_TEST(nonfinite_inputs_are_sanitized);
    RUN_TEST(large_opposite_signed_endpoints_stay_finite);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
