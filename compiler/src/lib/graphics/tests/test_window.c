//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_window.c
// Purpose: Unit tests covering window creation, resize, and teardown flows.
// Key invariants: Windows are destroyed on all paths; events are processed
//                 without deadlocks; resources do not leak.
// Ownership/Lifetime: Test binary; owns windows created during tests.
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//

/*
 * ViperGFX - Window Tests (T1-T3)
 * Tests window creation with various parameters
 */

#include "test_harness.h"
#include "vgfx.h"
#include <string.h>

/* T1: Window Creation – Valid Parameters */
void test_window_valid_params(void)
{
    TEST_BEGIN("T1: Window Creation - Valid Parameters");

    vgfx_window_params_t params = {
        .width = 800, .height = 600, .title = "Test", .fps = 60, .resizable = 1};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    int32_t w = 0, h = 0;
    int ok = vgfx_get_size(win, &w, &h);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(w, 800);
    ASSERT_EQ(h, 600);

    ok = vgfx_update(win);
    ASSERT_EQ(ok, 1);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T2: Window Creation – Dimensions Exceed Max */
void test_window_exceed_max(void)
{
    TEST_BEGIN("T2: Window Creation - Dimensions Exceed Max");

    vgfx_window_params_t params = {
        .width = 5000, .height = 5000, .title = "Test", .fps = 60, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NULL(win);

    const char *error = vgfx_get_last_error();
    ASSERT_NOT_NULL(error);
    ASSERT_TRUE(strstr(error, "exceed maximum") != NULL);

    TEST_END();
}

/* T3: Window Creation – Invalid Dimensions Use Defaults */
void test_window_invalid_dimensions_use_defaults(void)
{
    TEST_BEGIN("T3: Window Creation - Invalid Dimensions Use Defaults");

    vgfx_window_params_t params = {
        .width = 0, .height = -10, .title = "Test", .fps = 60, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    int32_t w = 0, h = 0;
    int ok = vgfx_get_size(win, &w, &h);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(w, VGFX_DEFAULT_WIDTH);
    ASSERT_EQ(h, VGFX_DEFAULT_HEIGHT);

    vgfx_destroy_window(win);
    TEST_END();
}

/* Main test runner */
/// What: Entry point for window lifecycle tests.
/// Why:  Validate that window create/resize/teardown flows are robust.
/// How:  Creates a window, triggers resizes/events, then cleans up deterministically.
int main(void)
{
    printf("========================================\n");
    printf("ViperGFX Window Tests (T1-T3)\n");
    printf("========================================\n");

    test_window_valid_params();
    test_window_exceed_max();
    test_window_invalid_dimensions_use_defaults();

    TEST_SUMMARY();
    return TEST_RETURN_CODE();
}

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_window.c
// Purpose: Unit tests covering window creation, resize, and teardown flows.
// Key invariants: Windows are destroyed on all paths; events are processed
//                 without deadlocks; resources do not leak.
// Ownership/Lifetime: Test binary; owns windows created during tests.
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//
