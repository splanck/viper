//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_input.c
// Purpose: Unit tests covering input events (keyboard/mouse) in ViperGFX.
// Key invariants: Avoid flakiness by simulating inputs when possible; assert
//                 on event sequencing and data integrity.
// Ownership/Lifetime: Test binary; creates/destroys windows as required.
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//

/*
 * ViperGFX - Input Tests (T16-T21)
 * Tests keyboard, mouse, and event queue with mock backend
 */

#include "test_harness.h"
#include "vgfx.h"
#include "vgfx_mock.h"

/* T16: Keyboard Input (Mock Backend) */
void test_keyboard_input(void)
{
    TEST_BEGIN("T16: Keyboard Input (Mock Backend)");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Inject KEY_DOWN for VGFX_KEY_A */
    vgfx_mock_inject_key_event(win, VGFX_KEY_A, 1);
    vgfx_update(win);

    /* Check key is down */
    ASSERT_EQ(vgfx_key_down(win, VGFX_KEY_A), 1);

    /* Inject KEY_UP for VGFX_KEY_A */
    vgfx_mock_inject_key_event(win, VGFX_KEY_A, 0);
    vgfx_update(win);

    /* Check key is up */
    ASSERT_EQ(vgfx_key_down(win, VGFX_KEY_A), 0);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T17: Mouse Position (Mock Backend) */
void test_mouse_position(void)
{
    TEST_BEGIN("T17: Mouse Position (Mock Backend)");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Case 1: Inside bounds */
    vgfx_mock_inject_mouse_move(win, 150, 200);
    vgfx_update(win);

    int32_t x = 0, y = 0;
    int ok = vgfx_mouse_pos(win, &x, &y);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(x, 150);
    ASSERT_EQ(y, 200);

    /* Case 2: Outside bounds */
    vgfx_mock_inject_mouse_move(win, -10, -10);
    vgfx_update(win);

    ok = vgfx_mouse_pos(win, &x, &y);
    ASSERT_EQ(ok, 0);
    ASSERT_EQ(x, -10);
    ASSERT_EQ(y, -10);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T18: Mouse Button (Mock Backend) */
void test_mouse_button(void)
{
    TEST_BEGIN("T18: Mouse Button (Mock Backend)");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Inject MOUSE_DOWN for left button */
    vgfx_mock_inject_mouse_button(win, VGFX_MOUSE_LEFT, 1);
    vgfx_update(win);

    ASSERT_EQ(vgfx_mouse_button(win, VGFX_MOUSE_LEFT), 1);

    /* Inject MOUSE_UP for left button */
    vgfx_mock_inject_mouse_button(win, VGFX_MOUSE_LEFT, 0);
    vgfx_update(win);

    ASSERT_EQ(vgfx_mouse_button(win, VGFX_MOUSE_LEFT), 0);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T19: Event Queue – Basic */
void test_event_queue_basic(void)
{
    TEST_BEGIN("T19: Event Queue - Basic");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Inject three events */
    vgfx_mock_inject_key_event(win, VGFX_KEY_A, 1);
    vgfx_mock_inject_mouse_move(win, 100, 200);
    vgfx_mock_inject_key_event(win, VGFX_KEY_A, 0);

    /* Update to process platform events */
    vgfx_update(win);

    /* Poll events in order */
    vgfx_event_t ev;
    int ok;

    ok = vgfx_poll_event(win, &ev);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(ev.type, VGFX_EVENT_KEY_DOWN);

    ok = vgfx_poll_event(win, &ev);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(ev.type, VGFX_EVENT_MOUSE_MOVE);

    ok = vgfx_poll_event(win, &ev);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(ev.type, VGFX_EVENT_KEY_UP);

    /* Queue should be empty */
    ok = vgfx_poll_event(win, &ev);
    ASSERT_EQ(ok, 0);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T20: Event Queue – Overflow */
void test_event_queue_overflow(void)
{
    TEST_BEGIN("T20: Event Queue - Overflow");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Inject VGFX_EVENT_QUEUE_SIZE + 44 events */
    int total_events = VGFX_EVENT_QUEUE_SIZE + 44;
    for (int i = 0; i < total_events; i++)
    {
        /* Alternate between two different keys to track which are dropped */
        vgfx_key_t key = (i < 44) ? VGFX_KEY_A : VGFX_KEY_B;
        vgfx_mock_inject_key_event(win, key, 1);
    }

    vgfx_update(win);

    /* Count delivered events - should be exactly VGFX_EVENT_QUEUE_SIZE */
    int delivered = 0;
    vgfx_event_t ev;
    while (vgfx_poll_event(win, &ev))
    {
        delivered++;
    }
    ASSERT_EQ(delivered, VGFX_EVENT_QUEUE_SIZE);

    /* Check overflow count */
    int32_t overflow = vgfx_event_overflow_count(win);
    ASSERT_EQ(overflow, 44);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T21: Resize Event */
void test_resize_event(void)
{
    TEST_BEGIN("T21: Resize Event");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 1};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Inject resize to 800x600 */
    vgfx_mock_inject_resize(win, 800, 600);
    vgfx_update(win);

    /* Poll resize event */
    vgfx_event_t ev;
    int ok = vgfx_poll_event(win, &ev);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(ev.type, VGFX_EVENT_RESIZE);
    ASSERT_EQ(ev.data.resize.width, 800);
    ASSERT_EQ(ev.data.resize.height, 600);

    /* Check window size updated */
    int32_t w = 0, h = 0;
    ok = vgfx_get_size(win, &w, &h);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(w, 800);
    ASSERT_EQ(h, 600);

    /* Check all pixels are black (framebuffer cleared) */
    vgfx_color_t color;
    for (int32_t y = 0; y < 100; y++)
    {
        for (int32_t x = 0; x < 100; x++)
        {
            ok = vgfx_point(win, x, y, &color);
            ASSERT_EQ(ok, 1);
            ASSERT_EQ(color, 0x000000);
        }
    }

    vgfx_destroy_window(win);
    TEST_END();
}

/* Main test runner */
/// What: Entry point for input tests covering key/mouse event handling.
/// Why:  Validate that the input subsystem reports and sequences events
///       correctly under typical usage.
/// How:  Creates a window, simulates or listens for events, and asserts on
///       observed behavior.
int main(void)
{
    printf("========================================\n");
    printf("ViperGFX Input Tests (T16-T21)\n");
    printf("========================================\n");

    test_keyboard_input();
    test_mouse_position();
    test_mouse_button();
    test_event_queue_basic();
    test_event_queue_overflow();
    test_resize_event();

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
// File: src/lib/graphics/tests/test_input.c
// Purpose: Unit tests covering input events (keyboard/mouse) in ViperGFX.
// Key invariants: Avoid flakiness by simulating inputs when possible; assert
//                 on event sequencing and data integrity.
// Ownership/Lifetime: Test binary; creates/destroys windows as required.
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//
