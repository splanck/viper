//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTSpriteAnimTests.cpp - Unit tests for rt_spriteanim
//===----------------------------------------------------------------------===//

#include "rt_spriteanim.h"
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
    rt_spriteanim sa = rt_spriteanim_new();
    ASSERT(sa != NULL);
    ASSERT(rt_spriteanim_is_playing(sa) == 0);
    ASSERT(rt_spriteanim_frame(sa) == 0);
    rt_spriteanim_destroy(sa);
}

TEST(setup)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 7, 6); // Frames 0-7, 6 ticks each

    ASSERT(rt_spriteanim_frame_count(sa) == 8);
    ASSERT(rt_spriteanim_frame_duration(sa) == 6);
    rt_spriteanim_destroy(sa);
}

TEST(play_stop)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 3, 4);

    rt_spriteanim_play(sa);
    ASSERT(rt_spriteanim_is_playing(sa) == 1);
    ASSERT(rt_spriteanim_frame(sa) == 0);

    rt_spriteanim_stop(sa);
    ASSERT(rt_spriteanim_is_playing(sa) == 0);
    rt_spriteanim_destroy(sa);
}

TEST(update_frames)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 3, 2); // 4 frames, 2 ticks each
    rt_spriteanim_play(sa);

    ASSERT(rt_spriteanim_frame(sa) == 0);

    // After 2 updates, should move to frame 1
    rt_spriteanim_update(sa);
    rt_spriteanim_update(sa);
    ASSERT(rt_spriteanim_frame(sa) == 1);
    ASSERT(rt_spriteanim_frame_changed(sa) == 1);

    rt_spriteanim_update(sa);
    ASSERT(rt_spriteanim_frame_changed(sa) == 0);
    rt_spriteanim_destroy(sa);
}

TEST(loop)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 1, 1); // 2 frames, 1 tick each
    rt_spriteanim_set_loop(sa, 1);
    rt_spriteanim_play(sa);

    rt_spriteanim_update(sa); // Frame 0 -> 1
    rt_spriteanim_update(sa); // Frame 1 -> 0 (loop)
    ASSERT(rt_spriteanim_frame(sa) == 0);
    ASSERT(rt_spriteanim_is_finished(sa) == 0);
    rt_spriteanim_destroy(sa);
}

TEST(one_shot)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 1, 1);
    rt_spriteanim_set_loop(sa, 0);
    rt_spriteanim_play(sa);

    rt_spriteanim_update(sa);              // Frame 0 -> 1
    ASSERT(rt_spriteanim_update(sa) == 1); // Finished
    ASSERT(rt_spriteanim_is_finished(sa) == 1);
    ASSERT(rt_spriteanim_frame(sa) == 1); // Stay at last frame
    rt_spriteanim_destroy(sa);
}

TEST(pingpong)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 2, 1); // 3 frames
    rt_spriteanim_set_pingpong(sa, 1);
    rt_spriteanim_play(sa);

    rt_spriteanim_update(sa); // 0 -> 1
    rt_spriteanim_update(sa); // 1 -> 2
    rt_spriteanim_update(sa); // 2 -> 1 (reverse)
    ASSERT(rt_spriteanim_frame(sa) == 1);
    rt_spriteanim_update(sa); // 1 -> 0
    ASSERT(rt_spriteanim_frame(sa) == 0);
    rt_spriteanim_destroy(sa);
}

TEST(pause_resume)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 3, 2);
    rt_spriteanim_play(sa);

    rt_spriteanim_update(sa);
    int64_t frame1 = rt_spriteanim_frame(sa);

    rt_spriteanim_pause(sa);
    ASSERT(rt_spriteanim_is_paused(sa) == 1);
    rt_spriteanim_update(sa);
    rt_spriteanim_update(sa);
    ASSERT(rt_spriteanim_frame(sa) == frame1); // No change

    rt_spriteanim_resume(sa);
    ASSERT(rt_spriteanim_is_paused(sa) == 0);
    rt_spriteanim_destroy(sa);
}

TEST(speed)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 3, 4); // 4 ticks per frame
    rt_spriteanim_set_speed(sa, 2.0); // 2x speed
    rt_spriteanim_play(sa);

    // At 2x speed, each update counts as 2 ticks
    // After 2 updates: 4 effective ticks = 1 frame advance
    rt_spriteanim_update(sa);
    rt_spriteanim_update(sa);
    ASSERT(rt_spriteanim_frame(sa) == 1);

    // After 2 more updates: 8 total effective ticks = 2 frame advances
    rt_spriteanim_update(sa);
    rt_spriteanim_update(sa);
    ASSERT(rt_spriteanim_frame(sa) == 2);
    rt_spriteanim_destroy(sa);
}

TEST(progress)
{
    rt_spriteanim sa = rt_spriteanim_new();
    rt_spriteanim_setup(sa, 0, 3, 1); // 4 frames (0-3), 1 tick each
    rt_spriteanim_play(sa);

    // Progress = (current - start) * 100 / (end - start)
    ASSERT(rt_spriteanim_progress(sa) == 0);  // Frame 0: 0/3 = 0%
    rt_spriteanim_update(sa);                 // Frame 1
    ASSERT(rt_spriteanim_progress(sa) == 33); // Frame 1: 1/3 = 33%
    rt_spriteanim_update(sa);                 // Frame 2
    ASSERT(rt_spriteanim_progress(sa) == 66); // Frame 2: 2/3 = 66%
    rt_spriteanim_destroy(sa);
}

int main()
{
    printf("RTSpriteAnimTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(setup);
    RUN_TEST(play_stop);
    RUN_TEST(update_frames);
    RUN_TEST(loop);
    RUN_TEST(one_shot);
    RUN_TEST(pingpong);
    RUN_TEST(pause_resume);
    RUN_TEST(speed);
    RUN_TEST(progress);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
