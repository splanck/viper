//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTScreenFXTests.cpp - Unit tests for rt_screenfx
//===----------------------------------------------------------------------===//

#include "rt_screenfx.h"
#include <cassert>
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

TEST(create_destroy)
{
    rt_screenfx fx = rt_screenfx_new();
    ASSERT(fx != NULL);
    ASSERT(rt_screenfx_is_active(fx) == 0);
    ASSERT(rt_screenfx_get_shake_x(fx) == 0);
    ASSERT(rt_screenfx_get_shake_y(fx) == 0);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == 0);
    rt_screenfx_destroy(fx);
}

TEST(shake)
{
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 10000, 100, 0); // 10 pixels, 100ms, no decay

    ASSERT(rt_screenfx_is_active(fx) == 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 1);

    // Update and check shake values change
    rt_screenfx_update(fx, 16);
    // Shake should produce non-zero offsets (though random)
    // After several updates, we expect some offset
    for (int i = 0; i < 5; i++)
    {
        rt_screenfx_update(fx, 16);
    }

    rt_screenfx_destroy(fx);
}

TEST(shake_decay)
{
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 10000, 200, 500); // 50% decay

    // Run until completion
    for (int i = 0; i < 20; i++)
    {
        rt_screenfx_update(fx, 16);
    }

    // After duration, should be inactive
    rt_screenfx_update(fx, 200);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 0);

    rt_screenfx_destroy(fx);
}

TEST(flash)
{
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_flash(fx, 0xFF0000FF, 100); // Red with alpha 255

    ASSERT(rt_screenfx_is_active(fx) == 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);

    rt_screenfx_update(fx, 10);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) > 0);

    // Run to completion
    rt_screenfx_update(fx, 100);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 0);

    rt_screenfx_destroy(fx);
}

TEST(fade_in)
{
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_fade_in(fx, 0x000000FF, 100); // Black, alpha 255

    rt_screenfx_update(fx, 10);
    int64_t alpha1 = rt_screenfx_get_overlay_alpha(fx);

    rt_screenfx_update(fx, 40);
    int64_t alpha2 = rt_screenfx_get_overlay_alpha(fx);

    // Fade-in: alpha should decrease over time
    ASSERT(alpha2 < alpha1);

    rt_screenfx_destroy(fx);
}

TEST(fade_out)
{
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_fade_out(fx, 0x000000FF, 100); // Black, alpha 255

    rt_screenfx_update(fx, 10);
    int64_t alpha1 = rt_screenfx_get_overlay_alpha(fx);

    rt_screenfx_update(fx, 40);
    int64_t alpha2 = rt_screenfx_get_overlay_alpha(fx);

    // Fade-out: alpha should increase over time
    ASSERT(alpha2 > alpha1);

    rt_screenfx_destroy(fx);
}

TEST(cancel_all)
{
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 5000, 500, 0);
    rt_screenfx_flash(fx, 0xFF0000FF, 500);

    ASSERT(rt_screenfx_is_active(fx) == 1);

    rt_screenfx_cancel_all(fx);
    ASSERT(rt_screenfx_is_active(fx) == 0);
    ASSERT(rt_screenfx_get_shake_x(fx) == 0);
    ASSERT(rt_screenfx_get_shake_y(fx) == 0);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == 0);

    rt_screenfx_destroy(fx);
}

TEST(cancel_type)
{
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 5000, 500, 0);
    rt_screenfx_flash(fx, 0xFF0000FF, 500);

    rt_screenfx_cancel_type(fx, RT_SCREENFX_SHAKE);

    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 0);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);

    rt_screenfx_destroy(fx);
}

TEST(multiple_effects)
{
    rt_screenfx fx = rt_screenfx_new();

    // Can have shake and flash active simultaneously
    rt_screenfx_shake(fx, 5000, 200, 0);
    rt_screenfx_flash(fx, 0xFF0000FF, 200);

    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);

    rt_screenfx_update(fx, 16);

    rt_screenfx_destroy(fx);
}

int main()
{
    printf("RTScreenFXTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(shake);
    RUN_TEST(shake_decay);
    RUN_TEST(flash);
    RUN_TEST(fade_in);
    RUN_TEST(fade_out);
    RUN_TEST(cancel_all);
    RUN_TEST(cancel_type);
    RUN_TEST(multiple_effects);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
