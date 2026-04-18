//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTKeyboardTests.cpp
// Purpose: Tests for Viper.Input.Keyboard static class.
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_input.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

extern "C" void rt_input_set_caps_lock_query_hook(int32_t (*hook)(void *canvas));
extern "C" void rt_input_reset_test_hooks(void);
extern "C" void rt_keyboard_clear_canvas_if_matches(void *canvas);

static int g_caps_lock_query_calls = 0;
static void *g_caps_lock_canvas = nullptr;
static int32_t g_caps_lock_value = 0;

extern "C" int32_t test_caps_lock_query_hook(void *canvas) {
    g_caps_lock_query_calls++;
    g_caps_lock_canvas = canvas;
    return g_caps_lock_value;
}

// ============================================================================
// Key Code Constants
// ============================================================================

static void test_key_constants() {
    // Test that key code getters return expected GLFW-compatible values
    assert(rt_keyboard_key_a() == 65);
    assert(rt_keyboard_key_z() == 90);
    assert(rt_keyboard_key_0() == 48);
    assert(rt_keyboard_key_9() == 57);
    assert(rt_keyboard_key_space() == 32);
    assert(rt_keyboard_key_enter() == 257);
    assert(rt_keyboard_key_escape() == 256);
    assert(rt_keyboard_key_up() == 265);
    assert(rt_keyboard_key_down() == 264);
    assert(rt_keyboard_key_left() == 263);
    assert(rt_keyboard_key_right() == 262);
    assert(rt_keyboard_key_f1() == 290);
    assert(rt_keyboard_key_f12() == 301);
    assert(rt_keyboard_key_lshift() == 340);
    assert(rt_keyboard_key_lctrl() == 341);
    assert(rt_keyboard_key_lalt() == 342);
    printf("test_key_constants: PASSED\n");
}

// ============================================================================
// Keyboard State - Initial State
// ============================================================================

static void test_initial_state() {
    // Initialize keyboard system
    rt_keyboard_init();

    // All keys should be up initially
    assert(rt_keyboard_is_down(rt_keyboard_key_a()) == 0);
    assert(rt_keyboard_is_up(rt_keyboard_key_a()) == 1);
    assert(rt_keyboard_any_down() == 0);
    assert(rt_keyboard_get_down() == 0);
    printf("test_initial_state: PASSED\n");
}

// ============================================================================
// Key Press/Release Events
// ============================================================================

static void test_key_press_release() {
    rt_keyboard_init();
    rt_keyboard_begin_frame();

    // Simulate key down event (using GLFW key code directly for testing)
    int64_t key_a = rt_keyboard_key_a();

    // Initially up
    assert(rt_keyboard_is_down(key_a) == 0);
    assert(rt_keyboard_is_up(key_a) == 1);

    // Simulate press - the on_key_down function expects vgfx codes, but we can
    // test the state tracking functions directly by accessing internal state
    // For now, we'll test the GetPressed/GetReleased functionality

    printf("test_key_press_release: PASSED\n");
}

// ============================================================================
// Per-Frame Event Tracking
// ============================================================================

static void test_frame_events() {
    rt_keyboard_init();

    // Begin a new frame - should reset pressed/released lists
    rt_keyboard_begin_frame();

    // GetPressed and GetReleased should return empty sequences
    void *pressed = rt_keyboard_get_pressed();
    void *released = rt_keyboard_get_released();

    assert(pressed != nullptr);
    assert(released != nullptr);
    assert(rt_seq_len(pressed) == 0);
    assert(rt_seq_len(released) == 0);

    printf("test_frame_events: PASSED\n");
}

// ============================================================================
// Key Name Helper
// ============================================================================

static void test_key_name() {
    // Test key name lookup
    rt_string name_a = rt_keyboard_key_name(rt_keyboard_key_a());
    assert(name_a != nullptr);
    // Should return "A"
    assert(rt_str_len(name_a) == 1);

    rt_string name_space = rt_keyboard_key_name(rt_keyboard_key_space());
    assert(name_space != nullptr);
    // Should return "Space"
    assert(rt_str_len(name_space) == 5);

    rt_string name_enter = rt_keyboard_key_name(rt_keyboard_key_enter());
    assert(name_enter != nullptr);
    // Should return "Enter"
    assert(rt_str_len(name_enter) == 5);

    rt_string name_f1 = rt_keyboard_key_name(rt_keyboard_key_f1());
    assert(name_f1 != nullptr);
    // Should return "F1"
    assert(rt_str_len(name_f1) == 2);

    // Unknown key should return "Unknown"
    rt_string name_unknown = rt_keyboard_key_name(-999);
    assert(name_unknown != nullptr);

    printf("test_key_name: PASSED\n");
}

// ============================================================================
// Modifier State
// ============================================================================

static void test_modifier_state() {
    rt_keyboard_init();

    // Initially all modifiers should be off
    assert(rt_keyboard_shift() == 0);
    assert(rt_keyboard_ctrl() == 0);
    assert(rt_keyboard_alt() == 0);
    // CapsLock state is platform-dependent, skip testing its initial value

    printf("test_modifier_state: PASSED\n");
}

static void test_caps_lock_query() {
    rt_input_reset_test_hooks();
    rt_keyboard_init();

    g_caps_lock_query_calls = 0;
    g_caps_lock_canvas = nullptr;
    g_caps_lock_value = 1;

    void *canvas = reinterpret_cast<void *>(0x1234);
    rt_input_set_caps_lock_query_hook(test_caps_lock_query_hook);
    rt_keyboard_set_canvas(canvas);

    assert(g_caps_lock_query_calls == 1);
    assert(g_caps_lock_canvas == canvas);
    assert(rt_keyboard_caps_lock() == 1);
    assert(g_caps_lock_query_calls == 2);

    g_caps_lock_value = 0;
    assert(rt_keyboard_caps_lock() == 0);
    assert(g_caps_lock_query_calls == 3);
    assert(g_caps_lock_canvas == canvas);

    rt_keyboard_set_canvas(nullptr);
    rt_input_reset_test_hooks();

    printf("test_caps_lock_query: PASSED\n");
}

static void test_canvas_detach() {
    rt_input_reset_test_hooks();
    rt_keyboard_init();

    g_caps_lock_query_calls = 0;
    g_caps_lock_canvas = nullptr;
    g_caps_lock_value = 1;

    void *canvas_a = reinterpret_cast<void *>(0x1111);
    void *canvas_b = reinterpret_cast<void *>(0x2222);
    rt_input_set_caps_lock_query_hook(test_caps_lock_query_hook);
    rt_keyboard_set_canvas(canvas_a);

    rt_keyboard_clear_canvas_if_matches(canvas_b);
    assert(rt_keyboard_caps_lock() == 1);
    assert(g_caps_lock_canvas == canvas_a);

    rt_keyboard_clear_canvas_if_matches(canvas_a);
    g_caps_lock_canvas = canvas_b;
    assert(rt_keyboard_caps_lock() == 1);
    assert(g_caps_lock_canvas == nullptr);

    rt_input_reset_test_hooks();
    printf("test_canvas_detach: PASSED\n");
}

// ============================================================================
// Text Input
// ============================================================================

static void test_text_input() {
    rt_keyboard_init();
    rt_keyboard_begin_frame();

    // Initially text input is disabled, so GetText should return empty
    rt_string text = rt_keyboard_get_text();
    assert(text != nullptr);
    assert(rt_str_len(text) == 0);

    // Enable text input
    rt_keyboard_enable_text_input();

    // Disable text input
    rt_keyboard_disable_text_input();

    printf("test_text_input: PASSED\n");
}

static void test_text_input_utf8() {
    rt_keyboard_init();
    rt_keyboard_begin_frame();
    rt_keyboard_enable_text_input();

    rt_keyboard_text_input(0x00E9);   // e acute
    rt_keyboard_text_input(0x1F642);  // slightly smiling face

    rt_string text = rt_keyboard_get_text();
    assert(text != nullptr);
    assert(rt_str_len(text) == 6);
    assert(std::strcmp(rt_string_cstr(text), "\xC3\xA9\xF0\x9F\x99\x82") == 0);

    rt_keyboard_disable_text_input();

    printf("test_text_input_utf8: PASSED\n");
}

// ============================================================================
// Boundary Cases
// ============================================================================

static void test_boundary_cases() {
    rt_keyboard_init();

    // Test invalid key codes
    assert(rt_keyboard_is_down(-1) == 0);
    assert(rt_keyboard_is_down(9999) == 0);
    assert(rt_keyboard_is_up(-1) == 1);
    assert(rt_keyboard_is_up(9999) == 1);
    assert(rt_keyboard_was_pressed(-1) == 0);
    assert(rt_keyboard_was_released(-1) == 0);

    printf("test_boundary_cases: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Viper.Input.Keyboard Tests ===\n\n");

    test_key_constants();
    test_initial_state();
    test_key_press_release();
    test_frame_events();
    test_key_name();
    test_modifier_state();
    test_caps_lock_query();
    test_canvas_detach();
    test_text_input();
    test_text_input_utf8();
    test_boundary_cases();

    printf("\nAll tests passed!\n");
    return 0;
}
