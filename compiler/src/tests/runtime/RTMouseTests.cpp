//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMouseTests.cpp
// Purpose: Tests for Viper.Input.Mouse static class.
//
//===----------------------------------------------------------------------===//

#include "rt_input.h"
#include "rt_internal.h"

#include <cassert>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Button Constants
// ============================================================================

static void test_button_constants()
{
    // Test button constant getters return expected values
    assert(rt_mouse_button_left() == 0);
    assert(rt_mouse_button_right() == 1);
    assert(rt_mouse_button_middle() == 2);
    assert(rt_mouse_button_x1() == 3);
    assert(rt_mouse_button_x2() == 4);
    printf("test_button_constants: PASSED\n");
}

// ============================================================================
// Initial State
// ============================================================================

static void test_initial_state()
{
    rt_mouse_init();

    // Position should be at origin initially
    assert(rt_mouse_x() == 0);
    assert(rt_mouse_y() == 0);
    assert(rt_mouse_delta_x() == 0);
    assert(rt_mouse_delta_y() == 0);

    // All buttons should be up initially
    assert(rt_mouse_is_down(VIPER_MOUSE_BUTTON_LEFT) == 0);
    assert(rt_mouse_is_down(VIPER_MOUSE_BUTTON_RIGHT) == 0);
    assert(rt_mouse_is_down(VIPER_MOUSE_BUTTON_MIDDLE) == 0);
    assert(rt_mouse_is_up(VIPER_MOUSE_BUTTON_LEFT) == 1);
    assert(rt_mouse_left() == 0);
    assert(rt_mouse_right() == 0);
    assert(rt_mouse_middle() == 0);

    // Wheel should be at zero
    assert(rt_mouse_wheel_x() == 0);
    assert(rt_mouse_wheel_y() == 0);

    // Cursor state
    assert(rt_mouse_is_hidden() == 0);
    assert(rt_mouse_is_captured() == 0);

    printf("test_initial_state: PASSED\n");
}

// ============================================================================
// Position Updates
// ============================================================================

static void test_position_updates()
{
    rt_mouse_init();
    rt_mouse_begin_frame();

    // Update position during frame
    rt_mouse_update_pos(100, 200);
    assert(rt_mouse_x() == 100);
    assert(rt_mouse_y() == 200);

    // Delta is calculated at start of begin_frame (x - prev_x)
    // After first begin_frame, delta = 0-0 = 0, and we updated to (100, 200)
    // On second begin_frame, delta = 100-0 = 100
    rt_mouse_begin_frame();
    assert(rt_mouse_delta_x() == 100);
    assert(rt_mouse_delta_y() == 200);

    // Now update to new position
    rt_mouse_update_pos(150, 250);

    // On third begin_frame, delta = 150-100 = 50
    rt_mouse_begin_frame();
    assert(rt_mouse_delta_x() == 50);
    assert(rt_mouse_delta_y() == 50);

    // Move back slightly
    rt_mouse_update_pos(140, 240);

    // On fourth begin_frame, delta = 140-150 = -10
    rt_mouse_begin_frame();
    assert(rt_mouse_delta_x() == -10);
    assert(rt_mouse_delta_y() == -10);

    printf("test_position_updates: PASSED\n");
}

// ============================================================================
// Button State
// ============================================================================

static void test_button_state()
{
    rt_mouse_init();
    rt_mouse_begin_frame();

    // Press left button
    rt_mouse_button_down(VIPER_MOUSE_BUTTON_LEFT);
    assert(rt_mouse_is_down(VIPER_MOUSE_BUTTON_LEFT) == 1);
    assert(rt_mouse_is_up(VIPER_MOUSE_BUTTON_LEFT) == 0);
    assert(rt_mouse_left() == 1);
    assert(rt_mouse_was_pressed(VIPER_MOUSE_BUTTON_LEFT) == 1);

    // Release left button
    rt_mouse_button_up(VIPER_MOUSE_BUTTON_LEFT);
    assert(rt_mouse_is_down(VIPER_MOUSE_BUTTON_LEFT) == 0);
    assert(rt_mouse_is_up(VIPER_MOUSE_BUTTON_LEFT) == 1);
    assert(rt_mouse_left() == 0);
    assert(rt_mouse_was_released(VIPER_MOUSE_BUTTON_LEFT) == 1);

    // New frame - events should be cleared
    rt_mouse_begin_frame();
    assert(rt_mouse_was_pressed(VIPER_MOUSE_BUTTON_LEFT) == 0);
    assert(rt_mouse_was_released(VIPER_MOUSE_BUTTON_LEFT) == 0);

    printf("test_button_state: PASSED\n");
}

// ============================================================================
// Click Detection
// ============================================================================

static void test_click_detection()
{
    rt_mouse_init();
    rt_mouse_begin_frame();

    // Quick press and release should be a click
    rt_mouse_button_down(VIPER_MOUSE_BUTTON_LEFT);
    rt_mouse_button_up(VIPER_MOUSE_BUTTON_LEFT);
    assert(rt_mouse_was_clicked(VIPER_MOUSE_BUTTON_LEFT) == 1);

    // New frame - click should be cleared
    rt_mouse_begin_frame();
    assert(rt_mouse_was_clicked(VIPER_MOUSE_BUTTON_LEFT) == 0);

    printf("test_click_detection: PASSED\n");
}

// ============================================================================
// Scroll Wheel
// ============================================================================

static void test_scroll_wheel()
{
    rt_mouse_init();
    rt_mouse_begin_frame();

    // Scroll up
    rt_mouse_update_wheel(0, 3);
    assert(rt_mouse_wheel_x() == 0);
    assert(rt_mouse_wheel_y() == 3);

    // Scroll more
    rt_mouse_update_wheel(2, -1);
    assert(rt_mouse_wheel_x() == 2);
    assert(rt_mouse_wheel_y() == 2);

    // New frame - wheel should reset
    rt_mouse_begin_frame();
    assert(rt_mouse_wheel_x() == 0);
    assert(rt_mouse_wheel_y() == 0);

    printf("test_scroll_wheel: PASSED\n");
}

// ============================================================================
// Cursor Control
// ============================================================================

static void test_cursor_control()
{
    rt_mouse_init();

    // Hide cursor
    rt_mouse_hide();
    assert(rt_mouse_is_hidden() == 1);

    // Show cursor
    rt_mouse_show();
    assert(rt_mouse_is_hidden() == 0);

    // Capture mouse
    rt_mouse_capture();
    assert(rt_mouse_is_captured() == 1);

    // Release mouse
    rt_mouse_release();
    assert(rt_mouse_is_captured() == 0);

    // Set position
    rt_mouse_set_pos(500, 300);
    assert(rt_mouse_x() == 500);
    assert(rt_mouse_y() == 300);

    printf("test_cursor_control: PASSED\n");
}

// ============================================================================
// Boundary Cases
// ============================================================================

static void test_boundary_cases()
{
    rt_mouse_init();

    // Invalid button indices
    assert(rt_mouse_is_down(-1) == 0);
    assert(rt_mouse_is_down(999) == 0);
    assert(rt_mouse_is_up(-1) == 1);
    assert(rt_mouse_is_up(999) == 1);
    assert(rt_mouse_was_pressed(-1) == 0);
    assert(rt_mouse_was_released(-1) == 0);
    assert(rt_mouse_was_clicked(-1) == 0);
    assert(rt_mouse_was_double_clicked(-1) == 0);

    // Invalid button operations should not crash
    rt_mouse_button_down(-1);
    rt_mouse_button_down(999);
    rt_mouse_button_up(-1);
    rt_mouse_button_up(999);

    printf("test_boundary_cases: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Input.Mouse Tests ===\n\n");

    test_button_constants();
    test_initial_state();
    test_position_updates();
    test_button_state();
    test_click_detection();
    test_scroll_wheel();
    test_cursor_control();
    test_boundary_cases();

    printf("\nAll tests passed!\n");
    return 0;
}
