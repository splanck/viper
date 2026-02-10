//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTActionPersistTests.cpp
// Purpose: Tests for action binding save/load persistence.
//
//===----------------------------------------------------------------------===//

#include "rt_action.h"
#include "rt_input.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

// ============================================================================
// Basic save/load roundtrip
// ============================================================================

static void test_save_empty()
{
    rt_action_init();
    rt_action_clear();

    rt_string json = rt_action_save();
    const char *cstr = rt_string_cstr(json);
    assert(cstr != nullptr);
    assert(strlen(cstr) > 0);
    /* Should produce valid JSON with empty actions array */
    assert(strstr(cstr, "\"actions\":[]") != nullptr);

    printf("test_save_empty: PASSED\n");
}

static void test_save_load_button_action()
{
    rt_action_init();
    rt_action_clear();

    /* Define a button action with key binding */
    rt_action_define(make_str("jump"));
    rt_action_bind_key(make_str("jump"), VIPER_KEY_SPACE);

    /* Save */
    rt_string json = rt_action_save();
    const char *cstr = rt_string_cstr(json);
    assert(strstr(cstr, "\"name\":\"jump\"") != nullptr);
    assert(strstr(cstr, "\"type\":\"button\"") != nullptr);
    assert(strstr(cstr, "\"type\":\"key\"") != nullptr);

    /* Clear and reload */
    rt_action_clear();
    assert(rt_action_exists(make_str("jump")) == 0);

    int8_t ok = rt_action_load(json);
    assert(ok == 1);
    assert(rt_action_exists(make_str("jump")) == 1);
    assert(rt_action_is_axis(make_str("jump")) == 0);
    assert(rt_action_binding_count(make_str("jump")) == 1);

    printf("test_save_load_button_action: PASSED\n");
}

static void test_save_load_axis_action()
{
    rt_action_init();
    rt_action_clear();

    /* Define an axis action with two key bindings */
    rt_action_define_axis(make_str("move_x"));
    rt_action_bind_key_axis(make_str("move_x"), VIPER_KEY_LEFT, -1.0);
    rt_action_bind_key_axis(make_str("move_x"), VIPER_KEY_RIGHT, 1.0);

    /* Save */
    rt_string json = rt_action_save();

    /* Clear and reload */
    rt_action_clear();
    int8_t ok = rt_action_load(json);
    assert(ok == 1);

    assert(rt_action_exists(make_str("move_x")) == 1);
    assert(rt_action_is_axis(make_str("move_x")) == 1);
    assert(rt_action_binding_count(make_str("move_x")) == 2);

    printf("test_save_load_axis_action: PASSED\n");
}

static void test_save_load_multiple_actions()
{
    rt_action_init();
    rt_action_clear();

    rt_action_define(make_str("fire"));
    rt_action_bind_key(make_str("fire"), VIPER_KEY_Z);
    rt_action_bind_mouse(make_str("fire"), VIPER_MOUSE_BUTTON_LEFT);

    rt_action_define(make_str("dodge"));
    rt_action_bind_key(make_str("dodge"), VIPER_KEY_X);

    rt_action_define_axis(make_str("look_x"));
    rt_action_bind_mouse_x(make_str("look_x"), 0.5);

    /* Save */
    rt_string json = rt_action_save();

    /* Clear and reload */
    rt_action_clear();
    int8_t ok = rt_action_load(json);
    assert(ok == 1);

    assert(rt_action_exists(make_str("fire")) == 1);
    assert(rt_action_exists(make_str("dodge")) == 1);
    assert(rt_action_exists(make_str("look_x")) == 1);
    assert(rt_action_binding_count(make_str("fire")) == 2);
    assert(rt_action_binding_count(make_str("dodge")) == 1);
    assert(rt_action_is_axis(make_str("look_x")) == 1);

    printf("test_save_load_multiple_actions: PASSED\n");
}

static void test_save_load_pad_bindings()
{
    rt_action_init();
    rt_action_clear();

    rt_action_define(make_str("jump"));
    rt_action_bind_pad_button(make_str("jump"), 0, VIPER_PAD_A);

    rt_action_define_axis(make_str("move_x"));
    rt_action_bind_pad_axis(make_str("move_x"), -1, VIPER_AXIS_LEFT_X, 1.0);

    rt_string json = rt_action_save();

    rt_action_clear();
    int8_t ok = rt_action_load(json);
    assert(ok == 1);

    assert(rt_action_exists(make_str("jump")) == 1);
    assert(rt_action_binding_count(make_str("jump")) == 1);
    assert(rt_action_exists(make_str("move_x")) == 1);
    assert(rt_action_binding_count(make_str("move_x")) == 1);

    printf("test_save_load_pad_bindings: PASSED\n");
}

// ============================================================================
// Edge cases
// ============================================================================

static void test_load_clears_existing()
{
    rt_action_init();
    rt_action_clear();

    rt_action_define(make_str("existing"));
    rt_action_define(make_str("other"));

    /* Save only "other" */
    rt_action_remove(make_str("existing"));
    rt_string json = rt_action_save();

    /* Restore with "existing" present */
    rt_action_define(make_str("existing"));
    assert(rt_action_exists(make_str("existing")) == 1);

    rt_action_load(json);
    /* "existing" should be gone (load clears first) */
    assert(rt_action_exists(make_str("existing")) == 0);
    assert(rt_action_exists(make_str("other")) == 1);

    printf("test_load_clears_existing: PASSED\n");
}

static void test_load_null_returns_zero()
{
    int8_t ok = rt_action_load(NULL);
    assert(ok == 0);

    printf("test_load_null_returns_zero: PASSED\n");
}

static void test_save_json_is_valid()
{
    rt_action_init();
    rt_action_clear();

    rt_action_define(make_str("test_action"));
    rt_action_bind_key(make_str("test_action"), VIPER_KEY_A);

    rt_string json = rt_action_save();
    const char *cstr = rt_string_cstr(json);

    /* Basic JSON structure validation */
    assert(cstr[0] == '{');
    assert(cstr[strlen(cstr) - 1] == '}');
    assert(strstr(cstr, "\"actions\"") != nullptr);

    printf("test_save_json_is_valid: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Action Persistence Tests ===\n\n");

    test_save_empty();
    test_save_load_button_action();
    test_save_load_axis_action();
    test_save_load_multiple_actions();
    test_save_load_pad_bindings();
    test_load_clears_existing();
    test_load_null_returns_zero();
    test_save_json_is_valid();

    printf("\nAll Action Persistence tests passed!\n");
    return 0;
}
