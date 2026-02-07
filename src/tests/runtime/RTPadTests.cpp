//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTPadTests.cpp
// Purpose: Tests for Viper.Input.Pad static class.
//
//===----------------------------------------------------------------------===//

#include "rt_input.h"
#include "rt_internal.h"

#include <cassert>
#include <cmath>
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
    assert(rt_pad_button_a() == 0);
    assert(rt_pad_button_b() == 1);
    assert(rt_pad_button_x() == 2);
    assert(rt_pad_button_y() == 3);
    assert(rt_pad_button_lb() == 4);
    assert(rt_pad_button_rb() == 5);
    assert(rt_pad_button_back() == 6);
    assert(rt_pad_button_start() == 7);
    assert(rt_pad_button_lstick() == 8);
    assert(rt_pad_button_rstick() == 9);
    assert(rt_pad_button_up() == 10);
    assert(rt_pad_button_down() == 11);
    assert(rt_pad_button_left() == 12);
    assert(rt_pad_button_right() == 13);
    assert(rt_pad_button_guide() == 14);
    printf("test_button_constants: PASSED\n");
}

// ============================================================================
// Initial State
// ============================================================================

static void test_initial_state()
{
    rt_pad_init();
    rt_pad_poll();

    int64_t count = rt_pad_count();
    assert(count >= 0 && count <= VIPER_PAD_MAX);

    // Invalid indices should return disconnected
    assert(rt_pad_is_connected(-1) == 0);
    assert(rt_pad_is_connected(4) == 0);
    assert(rt_pad_is_connected(999) == 0);

    for (int i = 0; i < VIPER_PAD_MAX; ++i)
    {
        if (!rt_pad_is_connected(i))
        {
            assert(rt_pad_is_down(i, VIPER_PAD_A) == 0);
            assert(rt_pad_is_up(i, VIPER_PAD_A) == 1);
            assert(rt_pad_was_pressed(i, VIPER_PAD_A) == 0);
            assert(rt_pad_was_released(i, VIPER_PAD_A) == 0);
            assert(rt_pad_left_x(i) == 0.0);
            assert(rt_pad_left_y(i) == 0.0);
            assert(rt_pad_right_x(i) == 0.0);
            assert(rt_pad_right_y(i) == 0.0);
            assert(rt_pad_left_trigger(i) == 0.0);
            assert(rt_pad_right_trigger(i) == 0.0);
        }
        else
        {
            assert(rt_pad_left_x(i) >= -1.0 && rt_pad_left_x(i) <= 1.0);
            assert(rt_pad_left_y(i) >= -1.0 && rt_pad_left_y(i) <= 1.0);
            assert(rt_pad_right_x(i) >= -1.0 && rt_pad_right_x(i) <= 1.0);
            assert(rt_pad_right_y(i) >= -1.0 && rt_pad_right_y(i) <= 1.0);
            assert(rt_pad_left_trigger(i) >= 0.0 && rt_pad_left_trigger(i) <= 1.0);
            assert(rt_pad_right_trigger(i) >= 0.0 && rt_pad_right_trigger(i) <= 1.0);
        }
    }

    printf("test_initial_state: PASSED\n");
}

// ============================================================================
// Deadzone Handling
// ============================================================================

static void test_deadzone()
{
    rt_pad_init();

    // Default deadzone should be 0.1
    assert(fabs(rt_pad_get_deadzone() - 0.1) < 0.001);

    // Set new deadzone
    rt_pad_set_deadzone(0.2);
    assert(fabs(rt_pad_get_deadzone() - 0.2) < 0.001);

    // Deadzone should be clamped to 0..1
    rt_pad_set_deadzone(-0.5);
    assert(rt_pad_get_deadzone() == 0.0);

    rt_pad_set_deadzone(1.5);
    assert(rt_pad_get_deadzone() == 1.0);

    // Reset to default
    rt_pad_set_deadzone(0.1);
    assert(fabs(rt_pad_get_deadzone() - 0.1) < 0.001);

    printf("test_deadzone: PASSED\n");
}

// ============================================================================
// Boundary Cases
// ============================================================================

static void test_boundary_cases()
{
    rt_pad_init();

    // Invalid controller indices should not crash
    assert(rt_pad_is_down(-1, VIPER_PAD_A) == 0);
    assert(rt_pad_is_down(999, VIPER_PAD_A) == 0);
    assert(rt_pad_is_up(-1, VIPER_PAD_A) == 1);
    assert(rt_pad_is_up(999, VIPER_PAD_A) == 1);

    // Invalid button indices should not crash
    assert(rt_pad_is_down(0, -1) == 0);
    assert(rt_pad_is_down(0, 999) == 0);
    assert(rt_pad_is_up(0, -1) == 1);
    assert(rt_pad_is_up(0, 999) == 1);
    assert(rt_pad_was_pressed(0, -1) == 0);
    assert(rt_pad_was_released(0, -1) == 0);

    // Analog reads on invalid indices should return 0
    assert(rt_pad_left_x(-1) == 0.0);
    assert(rt_pad_left_y(999) == 0.0);
    assert(rt_pad_right_x(-1) == 0.0);
    assert(rt_pad_right_y(999) == 0.0);
    assert(rt_pad_left_trigger(-1) == 0.0);
    assert(rt_pad_right_trigger(999) == 0.0);

    // Vibration on invalid indices should not crash
    rt_pad_vibrate(-1, 1.0, 1.0);
    rt_pad_vibrate(999, 1.0, 1.0);
    rt_pad_stop_vibration(-1);
    rt_pad_stop_vibration(999);

    // Name of invalid/disconnected controller
    rt_string name = rt_pad_name(0);
    assert(name != nullptr);
    assert(rt_str_len(name) == 0);

    name = rt_pad_name(-1);
    assert(name != nullptr);
    assert(rt_str_len(name) == 0);

    printf("test_boundary_cases: PASSED\n");
}

// ============================================================================
// Frame Reset
// ============================================================================

static void test_frame_reset()
{
    rt_pad_init();

    // Begin frame should reset per-frame state
    rt_pad_begin_frame();

    // Nothing should be pressed/released after frame reset
    assert(rt_pad_was_pressed(0, VIPER_PAD_A) == 0);
    assert(rt_pad_was_released(0, VIPER_PAD_A) == 0);

    printf("test_frame_reset: PASSED\n");
}

// ============================================================================
// Poll Function
// ============================================================================

static void test_poll()
{
    rt_pad_init();

    // Poll should not crash (stub implementation doesn't connect controllers)
    rt_pad_poll();

    // After poll, should still have no controllers (stub)
    assert(rt_pad_count() == 0);

    printf("test_poll: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Input.Pad Tests ===\n\n");

    test_button_constants();
    test_initial_state();
    test_deadzone();
    test_boundary_cases();
    test_frame_reset();
    test_poll();

    printf("\nAll tests passed!\n");
    return 0;
}
