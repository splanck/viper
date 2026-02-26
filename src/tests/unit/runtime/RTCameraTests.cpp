//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTCameraTests.cpp - Unit tests for rt_camera
//===----------------------------------------------------------------------===//

#include "rt_camera.h"
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

TEST(create)
{
    void *cam = rt_camera_new(800, 600);
    ASSERT(cam != NULL);
    ASSERT(rt_camera_get_x(cam) == 0);
    ASSERT(rt_camera_get_y(cam) == 0);
    ASSERT(rt_camera_get_zoom(cam) == 100);
    ASSERT(rt_camera_get_width(cam) == 800);
    ASSERT(rt_camera_get_height(cam) == 600);
    ASSERT(rt_camera_is_dirty(cam) == 1); // newly created camera is always dirty
}

TEST(is_visible_inside)
{
    void *cam = rt_camera_new(800, 600);
    // Camera at (0,0), zoom 100 → viewport covers world [0,0,800,600].
    // An object fully inside the viewport must be visible.
    ASSERT(rt_camera_is_visible(cam, 100, 100, 200, 200) == 1);
    // Object touching the right/bottom edge — still overlapping.
    ASSERT(rt_camera_is_visible(cam, 600, 400, 200, 200) == 1);
    // Object at origin.
    ASSERT(rt_camera_is_visible(cam, 0, 0, 1, 1) == 1);
}

TEST(is_visible_outside)
{
    void *cam = rt_camera_new(800, 600);
    // Viewport covers world [0,0,800,600].
    // Objects entirely off each edge must be invisible.
    ASSERT(rt_camera_is_visible(cam, 800, 0, 50, 50) == 0);  // off right
    ASSERT(rt_camera_is_visible(cam, 0, 600, 50, 50) == 0);  // off bottom
    ASSERT(rt_camera_is_visible(cam, -100, 0, 50, 50) == 0); // off left
    ASSERT(rt_camera_is_visible(cam, 0, -100, 50, 50) == 0); // off top
}

TEST(is_visible_partial_overlap)
{
    void *cam = rt_camera_new(800, 600);
    // Object partially hanging off the right edge — should still be visible.
    ASSERT(rt_camera_is_visible(cam, 780, 100, 100, 100) == 1); // right edge: 780+100=880 > 800
    // Object partially hanging off the bottom.
    ASSERT(rt_camera_is_visible(cam, 100, 580, 100, 100) == 1); // bottom: 580+100=680 > 600
    // Just one pixel inside the right edge.
    ASSERT(rt_camera_is_visible(cam, 799, 0, 10, 10) == 1);
}

TEST(is_visible_null_camera)
{
    // NULL camera must conservatively return 1 (visible).
    ASSERT(rt_camera_is_visible(NULL, 0, 0, 9999, 9999) == 1);
    ASSERT(rt_camera_is_visible(NULL, -1000, -1000, 1, 1) == 1);
}

TEST(is_visible_zoom_in)
{
    void *cam = rt_camera_new(800, 600);
    // Zoom in to 200%: world-space viewport = [0, 0, 400, 300].
    rt_camera_set_zoom(cam, 200);
    // Object at (450, 100) is inside 800×600 viewport but outside 400×300 — invisible.
    ASSERT(rt_camera_is_visible(cam, 450, 100, 50, 50) == 0);
    // Object at (100, 100) is inside 400×300 — visible.
    ASSERT(rt_camera_is_visible(cam, 100, 100, 50, 50) == 1);
}

TEST(is_visible_zoom_out)
{
    void *cam = rt_camera_new(800, 600);
    // Zoom out to 50%: world-space viewport = [0, 0, 1600, 1200].
    rt_camera_set_zoom(cam, 50);
    // Objects up to world coord 1600×1200 are now visible.
    ASSERT(rt_camera_is_visible(cam, 1500, 1100, 50, 50) == 1);
    // But beyond that range is still invisible.
    ASSERT(rt_camera_is_visible(cam, 1601, 0, 50, 50) == 0);
}

TEST(is_visible_with_camera_offset)
{
    void *cam = rt_camera_new(800, 600);
    // Move camera to world pos (1000, 500) → viewport covers [1000,500,1800,1100].
    rt_camera_set_x(cam, 1000);
    rt_camera_set_y(cam, 500);
    // Object at (1100, 600) — inside the offset viewport.
    ASSERT(rt_camera_is_visible(cam, 1100, 600, 100, 100) == 1);
    // Object at (0, 0) — far behind the camera, invisible.
    ASSERT(rt_camera_is_visible(cam, 0, 0, 100, 100) == 0);
    // Object just before the viewport left edge.
    ASSERT(rt_camera_is_visible(cam, 900, 500, 99, 100) == 0); // x+w=999 <= cam_x=1000
}

TEST(dirty_flag)
{
    void *cam = rt_camera_new(800, 600);
    ASSERT(rt_camera_is_dirty(cam) == 1); // starts dirty
    rt_camera_clear_dirty(cam);
    ASSERT(rt_camera_is_dirty(cam) == 0);
    rt_camera_set_x(cam, 100);
    ASSERT(rt_camera_is_dirty(cam) == 1);
    rt_camera_clear_dirty(cam);
    rt_camera_set_zoom(cam, 200);
    ASSERT(rt_camera_is_dirty(cam) == 1);
    rt_camera_clear_dirty(cam);
    rt_camera_set_rotation(cam, 45);
    ASSERT(rt_camera_is_dirty(cam) == 1);
}

int main()
{
    printf("RTCameraTests:\n");
    RUN_TEST(create);
    RUN_TEST(is_visible_inside);
    RUN_TEST(is_visible_outside);
    RUN_TEST(is_visible_partial_overlap);
    RUN_TEST(is_visible_null_camera);
    RUN_TEST(is_visible_zoom_in);
    RUN_TEST(is_visible_zoom_out);
    RUN_TEST(is_visible_with_camera_offset);
    RUN_TEST(dirty_flag);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
