//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime/RTActionMappingTests.cpp
// Purpose: Validate the action mapping system for input abstraction.
//
//===----------------------------------------------------------------------===//

#include "rt_action.h"
#include "rt_input.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

// Helper to create an rt_string from a C string
static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

// Test: Define button action and check existence
static void test_define_button_action()
{
    rt_action_init();
    rt_action_clear();

    rt_string jump = make_str("jump");
    assert(rt_action_define(jump) == 1);
    assert(rt_action_exists(jump) == 1);
    assert(rt_action_is_axis(jump) == 0);

    // Defining same action again should fail
    rt_string jump2 = make_str("jump");
    assert(rt_action_define(jump2) == 0);

    rt_action_clear();
}

// Test: Define axis action and check existence
static void test_define_axis_action()
{
    rt_action_init();
    rt_action_clear();

    rt_string move_x = make_str("move_x");
    assert(rt_action_define_axis(move_x) == 1);
    assert(rt_action_exists(move_x) == 1);
    assert(rt_action_is_axis(move_x) == 1);

    rt_action_clear();
}

// Test: Remove action
static void test_remove_action()
{
    rt_action_init();
    rt_action_clear();

    rt_string fire = make_str("fire");
    assert(rt_action_define(fire) == 1);
    assert(rt_action_exists(fire) == 1);

    rt_string fire2 = make_str("fire");
    assert(rt_action_remove(fire2) == 1);
    assert(rt_action_exists(fire) == 0);

    // Removing non-existent action should fail
    rt_string fire3 = make_str("fire");
    assert(rt_action_remove(fire3) == 0);

    rt_action_clear();
}

// Test: Bind keyboard key to button action
static void test_bind_key()
{
    rt_action_init();
    rt_action_clear();

    rt_string jump = make_str("jump");
    assert(rt_action_define(jump) == 1);

    rt_string jump2 = make_str("jump");
    assert(rt_action_bind_key(jump2, VIPER_KEY_SPACE) == 1);

    rt_string jump3 = make_str("jump");
    assert(rt_action_binding_count(jump3) == 1);

    // Binding to axis action with wrong function should fail
    rt_string move_x = make_str("move_x");
    assert(rt_action_define_axis(move_x) == 1);

    rt_string move_x2 = make_str("move_x");
    assert(rt_action_bind_key(move_x2, VIPER_KEY_LEFT) == 0); // Wrong - should use bind_key_axis

    rt_action_clear();
}

// Test: Bind key to axis action
static void test_bind_key_axis()
{
    rt_action_init();
    rt_action_clear();

    rt_string move_x = make_str("move_x");
    assert(rt_action_define_axis(move_x) == 1);

    rt_string move_x2 = make_str("move_x");
    assert(rt_action_bind_key_axis(move_x2, VIPER_KEY_LEFT, -1.0) == 1);

    rt_string move_x3 = make_str("move_x");
    assert(rt_action_bind_key_axis(move_x3, VIPER_KEY_RIGHT, 1.0) == 1);

    rt_string move_x4 = make_str("move_x");
    assert(rt_action_binding_count(move_x4) == 2);

    rt_action_clear();
}

// Test: Unbind key
static void test_unbind_key()
{
    rt_action_init();
    rt_action_clear();

    rt_string jump = make_str("jump");
    assert(rt_action_define(jump) == 1);

    rt_string jump2 = make_str("jump");
    assert(rt_action_bind_key(jump2, VIPER_KEY_SPACE) == 1);

    rt_string jump3 = make_str("jump");
    assert(rt_action_binding_count(jump3) == 1);

    rt_string jump4 = make_str("jump");
    assert(rt_action_unbind_key(jump4, VIPER_KEY_SPACE) == 1);

    rt_string jump5 = make_str("jump");
    assert(rt_action_binding_count(jump5) == 0);

    // Unbinding non-existent binding should fail
    rt_string jump6 = make_str("jump");
    assert(rt_action_unbind_key(jump6, VIPER_KEY_SPACE) == 0);

    rt_action_clear();
}

// Test: Bind mouse button
static void test_bind_mouse()
{
    rt_action_init();
    rt_action_clear();

    rt_string fire = make_str("fire");
    assert(rt_action_define(fire) == 1);

    rt_string fire2 = make_str("fire");
    assert(rt_action_bind_mouse(fire2, VIPER_MOUSE_BUTTON_LEFT) == 1);

    rt_string fire3 = make_str("fire");
    assert(rt_action_binding_count(fire3) == 1);

    rt_action_clear();
}

// Test: Bind gamepad button
static void test_bind_pad_button()
{
    rt_action_init();
    rt_action_clear();

    rt_string jump = make_str("jump");
    assert(rt_action_define(jump) == 1);

    // Bind to any controller (-1)
    rt_string jump2 = make_str("jump");
    assert(rt_action_bind_pad_button(jump2, -1, VIPER_PAD_A) == 1);

    rt_string jump3 = make_str("jump");
    assert(rt_action_binding_count(jump3) == 1);

    rt_action_clear();
}

// Test: Bind gamepad axis
static void test_bind_pad_axis()
{
    rt_action_init();
    rt_action_clear();

    rt_string move_x = make_str("move_x");
    assert(rt_action_define_axis(move_x) == 1);

    rt_string move_x2 = make_str("move_x");
    assert(rt_action_bind_pad_axis(move_x2, -1, VIPER_AXIS_LEFT_X, 1.0) == 1);

    rt_string move_x3 = make_str("move_x");
    assert(rt_action_binding_count(move_x3) == 1);

    rt_action_clear();
}

// Test: Multiple bindings for one action
static void test_multiple_bindings()
{
    rt_action_init();
    rt_action_clear();

    rt_string jump = make_str("jump");
    assert(rt_action_define(jump) == 1);

    rt_string jump2 = make_str("jump");
    rt_action_bind_key(jump2, VIPER_KEY_SPACE);

    rt_string jump3 = make_str("jump");
    rt_action_bind_key(jump3, VIPER_KEY_W);

    rt_string jump4 = make_str("jump");
    rt_action_bind_pad_button(jump4, -1, VIPER_PAD_A);

    rt_string jump5 = make_str("jump");
    assert(rt_action_binding_count(jump5) == 3);

    rt_action_clear();
}

// Test: Action bindings string
static void test_bindings_str()
{
    rt_action_init();
    rt_action_clear();

    rt_string jump = make_str("jump");
    assert(rt_action_define(jump) == 1);

    rt_string jump2 = make_str("jump");
    rt_action_bind_key(jump2, VIPER_KEY_SPACE);

    rt_string jump3 = make_str("jump");
    rt_action_bind_pad_button(jump3, -1, VIPER_PAD_A);

    rt_string jump4 = make_str("jump");
    rt_string bindings = rt_action_bindings_str(jump4);
    assert(rt_str_len(bindings) > 0);

    rt_action_clear();
}

// Test: Key bound to detection
static void test_key_bound_to()
{
    rt_action_init();
    rt_action_clear();

    rt_string jump = make_str("jump");
    assert(rt_action_define(jump) == 1);

    rt_string jump2 = make_str("jump");
    rt_action_bind_key(jump2, VIPER_KEY_SPACE);

    rt_string bound = rt_action_key_bound_to(VIPER_KEY_SPACE);
    assert(rt_str_len(bound) > 0);

    // Unbound key should return empty
    rt_string unbound = rt_action_key_bound_to(VIPER_KEY_A);
    assert(rt_str_len(unbound) == 0);

    rt_action_clear();
}

// Test: Axis constant getters
static void test_axis_constants()
{
    assert(rt_action_axis_left_x() == VIPER_AXIS_LEFT_X);
    assert(rt_action_axis_left_y() == VIPER_AXIS_LEFT_Y);
    assert(rt_action_axis_right_x() == VIPER_AXIS_RIGHT_X);
    assert(rt_action_axis_right_y() == VIPER_AXIS_RIGHT_Y);
    assert(rt_action_axis_left_trigger() == VIPER_AXIS_LEFT_TRIGGER);
    assert(rt_action_axis_right_trigger() == VIPER_AXIS_RIGHT_TRIGGER);
}

// Test: Action system lifecycle
static void test_lifecycle()
{
    rt_action_init();

    rt_string test = make_str("test");
    assert(rt_action_define(test) == 1);

    rt_action_shutdown();

    // After shutdown, init should work again
    rt_action_init();

    rt_string test2 = make_str("test");
    assert(rt_action_exists(test2) == 0); // Should be cleared

    rt_action_shutdown();
}

// Test: Empty/null action name handling
static void test_invalid_names()
{
    rt_action_init();
    rt_action_clear();

    // NULL name should fail
    assert(rt_action_define(NULL) == 0);
    assert(rt_action_define_axis(NULL) == 0);
    assert(rt_action_exists(NULL) == 0);
    assert(rt_action_remove(NULL) == 0);

    // Empty string should fail
    rt_string empty = rt_str_empty();
    assert(rt_action_define(empty) == 0);
    assert(rt_action_define_axis(empty) == 0);

    rt_action_clear();
}

int main()
{
    // Initialize keyboard for key name lookups
    rt_keyboard_init();

    test_define_button_action();
    test_define_axis_action();
    test_remove_action();
    test_bind_key();
    test_bind_key_axis();
    test_unbind_key();
    test_bind_mouse();
    test_bind_pad_button();
    test_bind_pad_axis();
    test_multiple_bindings();
    test_bindings_str();
    test_key_bound_to();
    test_axis_constants();
    test_lifecycle();
    test_invalid_names();

    return 0;
}
