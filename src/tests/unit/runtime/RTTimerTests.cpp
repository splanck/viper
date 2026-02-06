//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime/RTTimerTests.cpp
// Purpose: Validate the frame-based Timer class.
//
//===----------------------------------------------------------------------===//

#include "rt_timer.h"

#include <cassert>
#include <cstdio>

static void test_create_and_destroy()
{
    printf("  test_create_and_destroy...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    // Initial state
    assert(rt_timer_is_running(timer) == 0);
    assert(rt_timer_elapsed(timer) == 0);
    assert(rt_timer_remaining(timer) == 0);
    assert(rt_timer_duration(timer) == 0);

    rt_timer_destroy(timer);
}

static void test_start_and_update()
{
    printf("  test_start_and_update...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 10);

    assert(rt_timer_is_running(timer) == 1);
    assert(rt_timer_duration(timer) == 10);
    assert(rt_timer_elapsed(timer) == 0);
    assert(rt_timer_remaining(timer) == 10);

    // Update a few times
    assert(rt_timer_update(timer) == 0); // elapsed = 1
    assert(rt_timer_update(timer) == 0); // elapsed = 2
    assert(rt_timer_update(timer) == 0); // elapsed = 3

    assert(rt_timer_elapsed(timer) == 3);
    assert(rt_timer_remaining(timer) == 7);
    assert(rt_timer_is_running(timer) == 1);

    rt_timer_destroy(timer);
}

static void test_expiration()
{
    printf("  test_expiration...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 5);

    // Update until expiration
    assert(rt_timer_update(timer) == 0); // 1
    assert(rt_timer_update(timer) == 0); // 2
    assert(rt_timer_update(timer) == 0); // 3
    assert(rt_timer_update(timer) == 0); // 4
    assert(rt_timer_update(timer) == 1); // 5 - expires!

    assert(rt_timer_is_running(timer) == 0);
    assert(rt_timer_is_expired(timer) == 1);
    assert(rt_timer_remaining(timer) == 0);

    // Further updates should not return true again
    assert(rt_timer_update(timer) == 0);

    rt_timer_destroy(timer);
}

static void test_progress()
{
    printf("  test_progress...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 100);

    assert(rt_timer_progress(timer) == 0);

    // Update to 25%
    for (int i = 0; i < 25; i++)
    {
        rt_timer_update(timer);
    }
    assert(rt_timer_progress(timer) == 25);

    // Update to 50%
    for (int i = 0; i < 25; i++)
    {
        rt_timer_update(timer);
    }
    assert(rt_timer_progress(timer) == 50);

    // Update to 100%
    for (int i = 0; i < 50; i++)
    {
        rt_timer_update(timer);
    }
    assert(rt_timer_progress(timer) == 100);

    rt_timer_destroy(timer);
}

static void test_stop()
{
    printf("  test_stop...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 100);
    rt_timer_update(timer);
    rt_timer_update(timer);

    assert(rt_timer_is_running(timer) == 1);

    rt_timer_stop(timer);

    assert(rt_timer_is_running(timer) == 0);
    assert(rt_timer_elapsed(timer) == 2); // Preserved

    // Updates should do nothing when stopped
    rt_timer_update(timer);
    assert(rt_timer_elapsed(timer) == 2);

    rt_timer_destroy(timer);
}

static void test_reset()
{
    printf("  test_reset...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 100);
    for (int i = 0; i < 50; i++)
    {
        rt_timer_update(timer);
    }

    assert(rt_timer_elapsed(timer) == 50);

    rt_timer_reset(timer);

    assert(rt_timer_elapsed(timer) == 0);
    assert(rt_timer_is_running(timer) == 1); // Still running
    assert(rt_timer_remaining(timer) == 100);

    rt_timer_destroy(timer);
}

static void test_repeating_timer()
{
    printf("  test_repeating_timer...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start_repeating(timer, 5);
    assert(rt_timer_is_repeating(timer) == 1);

    // First cycle
    assert(rt_timer_update(timer) == 0); // 1
    assert(rt_timer_update(timer) == 0); // 2
    assert(rt_timer_update(timer) == 0); // 3
    assert(rt_timer_update(timer) == 0); // 4
    assert(rt_timer_update(timer) == 1); // 5 - fires, resets

    // Should still be running and elapsed reset to 0
    assert(rt_timer_is_running(timer) == 1);
    assert(rt_timer_elapsed(timer) == 0);

    // Second cycle
    assert(rt_timer_update(timer) == 0); // 1
    assert(rt_timer_update(timer) == 0); // 2
    assert(rt_timer_update(timer) == 0); // 3
    assert(rt_timer_update(timer) == 0); // 4
    assert(rt_timer_update(timer) == 1); // 5 - fires again

    assert(rt_timer_is_running(timer) == 1);

    rt_timer_destroy(timer);
}

static void test_non_repeating_timer()
{
    printf("  test_non_repeating_timer...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 5);
    assert(rt_timer_is_repeating(timer) == 0);

    // Run to expiration
    for (int i = 0; i < 5; i++)
    {
        rt_timer_update(timer);
    }

    // Should not be running anymore
    assert(rt_timer_is_running(timer) == 0);
    assert(rt_timer_is_expired(timer) == 1);

    rt_timer_destroy(timer);
}

static void test_set_duration()
{
    printf("  test_set_duration...\n");

    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 100);
    rt_timer_update(timer);
    rt_timer_update(timer);

    assert(rt_timer_duration(timer) == 100);

    rt_timer_set_duration(timer, 50);

    assert(rt_timer_duration(timer) == 50);
    assert(rt_timer_elapsed(timer) == 2); // Preserved
    assert(rt_timer_remaining(timer) == 48);

    rt_timer_destroy(timer);
}

static void test_animation_use_case()
{
    printf("  test_animation_use_case...\n");

    // Simulate a 60-frame animation (1 second at 60fps)
    rt_timer timer = rt_timer_new();
    assert(timer != nullptr);

    rt_timer_start(timer, 60);

    int frame_count = 0;
    while (rt_timer_is_running(timer))
    {
        // Calculate animation progress (0-100)
        int64_t progress = rt_timer_progress(timer);
        assert(progress >= 0 && progress <= 100);

        rt_timer_update(timer);
        frame_count++;

        if (frame_count > 100)
            break; // Safety limit
    }

    assert(frame_count == 60);
    assert(rt_timer_is_expired(timer) == 1);

    rt_timer_destroy(timer);
}

static void test_ghost_mode_timer_use_case()
{
    printf("  test_ghost_mode_timer_use_case...\n");

    // Simulate ghost mode switching (frightened mode for 600 frames)
    rt_timer frightened_timer = rt_timer_new();
    assert(frightened_timer != nullptr);

    // Ghost eats power pellet
    rt_timer_start(frightened_timer, 600);

    // Simulate 300 frames (halfway through)
    for (int i = 0; i < 300; i++)
    {
        assert(rt_timer_update(frightened_timer) == 0);
    }

    assert(rt_timer_progress(frightened_timer) == 50);
    assert(rt_timer_is_running(frightened_timer) == 1);

    // Simulate remaining 300 frames
    int expired = 0;
    for (int i = 0; i < 300; i++)
    {
        if (rt_timer_update(frightened_timer))
        {
            expired = 1;
        }
    }

    assert(expired == 1);
    assert(rt_timer_is_running(frightened_timer) == 0);

    rt_timer_destroy(frightened_timer);
}

int main()
{
    printf("RTTimerTests:\n");

    test_create_and_destroy();
    test_start_and_update();
    test_expiration();
    test_progress();
    test_stop();
    test_reset();
    test_repeating_timer();
    test_non_repeating_timer();
    test_set_duration();
    test_animation_use_case();
    test_ghost_mode_timer_use_case();

    printf("All Timer tests passed!\n");
    return 0;
}
