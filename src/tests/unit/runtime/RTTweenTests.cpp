//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTTweenTests.cpp - Unit tests for rt_tween
//===----------------------------------------------------------------------===//

#include "rt_tween.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name)                                                                             \
    do                                                                                             \
    {                                                                                              \
        printf("  %s...", #name);                                                                  \
        test_##name();                                                                             \
        printf(" OK\n");                                                                           \
        tests_passed++;                                                                            \
    } while (0)

#define ASSERT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf(" FAILED at line %d: %s\n", __LINE__, #cond);                                   \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabs((a) - (b)) < (eps))

TEST(create_destroy)
{
    rt_tween tw = rt_tween_new();
    ASSERT(tw != NULL);
    ASSERT(rt_tween_is_running(tw) == 0);
    ASSERT(rt_tween_is_complete(tw) == 0);
    rt_tween_destroy(tw);
}

TEST(start_linear)
{
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

TEST(start_i64)
{
    rt_tween tw = rt_tween_new();
    rt_tween_start_i64(tw, 0, 200, 20, RT_EASE_LINEAR);

    for (int i = 0; i < 10; i++)
        rt_tween_update(tw);
    ASSERT(rt_tween_value_i64(tw) == 100);
    rt_tween_destroy(tw);
}

TEST(pause_resume)
{
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

TEST(stop_reset)
{
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

TEST(ease_functions)
{
    // Test that different easing types produce different curves
    double t = 0.5;
    double linear = rt_tween_ease(t, RT_EASE_LINEAR);
    double in_quad = rt_tween_ease(t, RT_EASE_IN_QUAD);
    double out_quad = rt_tween_ease(t, RT_EASE_OUT_QUAD);

    ASSERT_NEAR(linear, 0.5, 0.001);
    ASSERT(in_quad < linear);  // ease-in is slower at midpoint
    ASSERT(out_quad > linear); // ease-out is faster at midpoint
}

TEST(lerp_i64)
{
    ASSERT(rt_tween_lerp_i64(0, 100, 0.0) == 0);
    ASSERT(rt_tween_lerp_i64(0, 100, 0.5) == 50);
    ASSERT(rt_tween_lerp_i64(0, 100, 1.0) == 100);
    ASSERT(rt_tween_lerp_i64(-100, 100, 0.5) == 0);
}

int main()
{
    printf("RTTweenTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(start_linear);
    RUN_TEST(start_i64);
    RUN_TEST(pause_resume);
    RUN_TEST(stop_reset);
    RUN_TEST(ease_functions);
    RUN_TEST(lerp_i64);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
