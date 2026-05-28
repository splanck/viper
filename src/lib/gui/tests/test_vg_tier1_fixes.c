//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_tier1_fixes.c
// Purpose: Tier 1 GUI widget fix validation tests — button keyboard activation,
//          slider keyboard navigation, listbox keyboard/mouse/virtual-mode,
//          textinput Shift+select, Ctrl+word-jump, and UTF-8 editing.
// Key invariants:
//   - BUG-GUI-003: Space/Enter activate a focused button
//   - BUG-GUI-004: Arrow/Home/End keys drive slider value with step clamping
//   - BUG-GUI-005: Arrow/Home/End keys drive listbox selection
//   - BUG-GUI-007: TextInput Shift+select and Ctrl+word-jump behave correctly
//   - BUG-GUI-001: label word_wrap field exists and defaults to false
// Ownership/Lifetime:
//   - Each test creates and destroys its own widgets
// Links: lib/gui/include/vg_widget.h, lib/gui/include/vg_widgets.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "vg_event.h"
#include "vg_ide_widgets.h"
#include "vg_widget.h"
#include "vg_widgets.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Test Harness
//=============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name)                                                                                  \
    do {                                                                                           \
        printf("  %-60s", #name "...");                                                            \
        fflush(stdout);                                                                            \
        test_##name();                                                                             \
        if (g_failed == 0)                                                                         \
            printf("OK\n");                                                                        \
        g_passed++;                                                                                \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n  (%s:%d: %s)\n", __FILE__, __LINE__, #cond);                            \
            g_failed++;                                                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NEQ(a, b) ASSERT((a) != (b))
#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_TRUE(cond) ASSERT(cond)
#define ASSERT_FALSE(cond) ASSERT(!(cond))

//=============================================================================
// Helper: build a key-down event
//=============================================================================

/// @brief Build a VG_EVENT_KEY_DOWN event for the given key and modifier mask.
static vg_event_t make_key_down(vg_key_t key, uint32_t mods) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_KEY_DOWN;
    ev.key.key = key;
    ev.modifiers = mods;
    return ev;
}

/// @brief Build a VG_EVENT_KEY_CHAR event carrying the given Unicode codepoint.
static vg_event_t make_key_char(uint32_t codepoint) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_KEY_CHAR;
    ev.key.codepoint = codepoint;
    return ev;
}

/// @brief Build a synthetic VG_EVENT_CLICK event.
static vg_event_t make_click(void) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_CLICK;
    return ev;
}

/// @brief Build a VG_EVENT_MOUSE_DOWN event at the given screen coordinates.
static vg_event_t make_mouse_down(float screen_x, float screen_y) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_MOUSE_DOWN;
    ev.mouse.screen_x = screen_x;
    ev.mouse.screen_y = screen_y;
    return ev;
}

/// @brief Build a VG_EVENT_MOUSE_WHEEL event with the given vertical delta.
static vg_event_t make_mouse_wheel(float delta_y) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_MOUSE_WHEEL;
    ev.wheel.delta_y = delta_y;
    return ev;
}

//=============================================================================
// BUG-GUI-003 — Button keyboard activation
//=============================================================================

static int g_button_clicked = 0;

/// @brief on_click callback that increments g_button_clicked to confirm activation.
static void button_click_cb(vg_widget_t *w, void *data) {
    (void)w;
    (void)data;
    g_button_clicked++;
}

/// @brief BUG-GUI-003 — button vtable exposes can_focus and returns true.
TEST(button_can_focus) {
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    ASSERT_NOT_NULL(btn->base.vtable->can_focus);
    ASSERT_TRUE(btn->base.vtable->can_focus(&btn->base));
    vg_widget_destroy(&btn->base);
}

/// @brief BUG-GUI-003 — Space key fires the on_click callback.
TEST(button_space_activates) {
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    vg_button_set_on_click(btn, button_click_cb, NULL);

    g_button_clicked = 0;
    vg_event_t ev = make_key_down(VG_KEY_SPACE, VG_MOD_NONE);
    bool handled = btn->base.vtable->handle_event(&btn->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_EQ(g_button_clicked, 1);

    vg_widget_destroy(&btn->base);
}

/// @brief BUG-GUI-003 — Enter key fires the on_click callback.
TEST(button_enter_activates) {
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    vg_button_set_on_click(btn, button_click_cb, NULL);

    g_button_clicked = 0;
    vg_event_t ev = make_key_down(VG_KEY_ENTER, VG_MOD_NONE);
    bool handled = btn->base.vtable->handle_event(&btn->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_EQ(g_button_clicked, 1);

    vg_widget_destroy(&btn->base);
}

/// @brief BUG-GUI-003 — an unrelated key (e.g. A) does not invoke the on_click callback.
TEST(button_other_key_does_nothing) {
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    vg_button_set_on_click(btn, button_click_cb, NULL);

    g_button_clicked = 0;
    vg_event_t ev = make_key_down(VG_KEY_A, VG_MOD_NONE);
    btn->base.vtable->handle_event(&btn->base, &ev);
    ASSERT_EQ(g_button_clicked, 0);

    vg_widget_destroy(&btn->base);
}

/// @brief BUG-GUI-003 — direct CLICK event still fires the callback (regression guard).
TEST(button_click_still_works) {
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    vg_button_set_on_click(btn, button_click_cb, NULL);

    g_button_clicked = 0;
    vg_event_t ev = make_click();
    bool handled = btn->base.vtable->handle_event(&btn->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_EQ(g_button_clicked, 1);

    vg_widget_destroy(&btn->base);
}

//=============================================================================
// BUG-GUI-004 — Slider focus + keyboard navigation
//=============================================================================

/// @brief BUG-GUI-004 — horizontal slider vtable exposes can_focus and returns true.
TEST(slider_can_focus) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->base.vtable->can_focus);
    ASSERT_TRUE(s->base.vtable->can_focus(&s->base));
    vg_widget_destroy(&s->base);
}

/// @brief BUG-GUI-004 — Right arrow key increases the slider value above its starting point.
TEST(slider_right_key_increases_value) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_value(s, 50.0f);

    vg_event_t ev = make_key_down(VG_KEY_RIGHT, VG_MOD_NONE);
    bool handled = s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_TRUE(vg_slider_get_value(s) > 50.0f);

    vg_widget_destroy(&s->base);
}

/// @brief BUG-GUI-004 — Left arrow key decreases the slider value below its starting point.
TEST(slider_left_key_decreases_value) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_value(s, 50.0f);

    vg_event_t ev = make_key_down(VG_KEY_LEFT, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_TRUE(vg_slider_get_value(s) < 50.0f);

    vg_widget_destroy(&s->base);
}

/// @brief BUG-GUI-004 — Home key jumps the slider to its minimum value.
TEST(slider_home_jumps_to_min) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 10.0f, 200.0f);
    vg_slider_set_value(s, 150.0f);

    vg_event_t ev = make_key_down(VG_KEY_HOME, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_EQ((int)vg_slider_get_value(s), 10);

    vg_widget_destroy(&s->base);
}

/// @brief BUG-GUI-004 — End key jumps the slider to its maximum value.
TEST(slider_end_jumps_to_max) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_value(s, 30.0f);

    vg_event_t ev = make_key_down(VG_KEY_END, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_EQ((int)vg_slider_get_value(s), 100);

    vg_widget_destroy(&s->base);
}

/// @brief BUG-GUI-004 — Right arrow increments by step (5) when a step size is configured.
TEST(slider_key_respects_step) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_step(s, 5.0f);
    vg_slider_set_value(s, 50.0f);

    vg_event_t ev = make_key_down(VG_KEY_RIGHT, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_EQ((int)vg_slider_get_value(s), 55);

    vg_widget_destroy(&s->base);
}

/// @brief BUG-GUI-004 — Right arrow at maximum does not push value beyond the upper bound.
TEST(slider_clamps_at_max) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_value(s, 100.0f);

    vg_event_t ev = make_key_down(VG_KEY_RIGHT, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_EQ((int)vg_slider_get_value(s), 100); // must not exceed max

    vg_widget_destroy(&s->base);
}

//=============================================================================
// BUG-GUI-005 — ListBox focus + keyboard navigation
//=============================================================================

/// @brief BUG-GUI-005 — listbox vtable exposes can_focus and returns true.
TEST(listbox_can_focus) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    ASSERT_NOT_NULL(lb->base.vtable->can_focus);
    ASSERT_TRUE(lb->base.vtable->can_focus(&lb->base));
    vg_widget_destroy(&lb->base);
}

/// @brief BUG-GUI-005 — Down arrow moves selection from index 0 to index 1.
TEST(listbox_down_key_selects_next) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "Item 0", NULL);
    vg_listbox_add_item(lb, "Item 1", NULL);
    vg_listbox_add_item(lb, "Item 2", NULL);

    // Select first item
    vg_listbox_select_index(lb, 0);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 0);

    lb->base.width = 200.0f;
    lb->base.height = 200.0f;

    vg_event_t ev = make_key_down(VG_KEY_DOWN, VG_MOD_NONE);
    bool handled = lb->base.vtable->handle_event(&lb->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 1);

    vg_widget_destroy(&lb->base);
}

/// @brief BUG-GUI-005 — Up arrow moves selection from index 2 to index 1.
TEST(listbox_up_key_selects_prev) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "A", NULL);
    vg_listbox_add_item(lb, "B", NULL);
    vg_listbox_add_item(lb, "C", NULL);

    vg_listbox_select_index(lb, 2);
    lb->base.width = 200.0f;
    lb->base.height = 200.0f;

    vg_event_t ev = make_key_down(VG_KEY_UP, VG_MOD_NONE);
    lb->base.vtable->handle_event(&lb->base, &ev);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 1);

    vg_widget_destroy(&lb->base);
}

/// @brief BUG-GUI-005 — Home key selects index 0 regardless of current selection.
TEST(listbox_home_key_selects_first) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "X", NULL);
    vg_listbox_add_item(lb, "Y", NULL);
    vg_listbox_add_item(lb, "Z", NULL);

    vg_listbox_select_index(lb, 2);
    lb->base.width = 200.0f;
    lb->base.height = 200.0f;

    vg_event_t ev = make_key_down(VG_KEY_HOME, VG_MOD_NONE);
    lb->base.vtable->handle_event(&lb->base, &ev);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 0);

    vg_widget_destroy(&lb->base);
}

/// @brief BUG-GUI-005 — End key selects the last index regardless of current selection.
TEST(listbox_end_key_selects_last) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "P", NULL);
    vg_listbox_add_item(lb, "Q", NULL);
    vg_listbox_add_item(lb, "R", NULL);

    vg_listbox_select_index(lb, 0);
    lb->base.width = 200.0f;
    lb->base.height = 200.0f;

    vg_event_t ev = make_key_down(VG_KEY_END, VG_MOD_NONE);
    lb->base.vtable->handle_event(&lb->base, &ev);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 2);

    vg_widget_destroy(&lb->base);
}

/// @brief BUG-GUI-005 — Up from index 0 and Down from the last index both clamp to avoid
/// out-of-bounds.
TEST(listbox_clamps_at_ends) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "Only", NULL);

    vg_listbox_select_index(lb, 0);
    lb->base.width = 200.0f;
    lb->base.height = 200.0f;

    /* Up from 0 should stay at 0 */
    vg_event_t up = make_key_down(VG_KEY_UP, VG_MOD_NONE);
    lb->base.vtable->handle_event(&lb->base, &up);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 0);

    /* Down from last (0) should stay at 0 */
    vg_event_t dn = make_key_down(VG_KEY_DOWN, VG_MOD_NONE);
    lb->base.vtable->handle_event(&lb->base, &dn);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 0);

    vg_widget_destroy(&lb->base);
}

//=============================================================================
// BUG-GUI-007 — TextInput Shift+select and Ctrl+word-jump
//=============================================================================

/// @brief BUG-GUI-007 — Shift+Left moves the cursor left and extends the selection anchor
/// rightward.
TEST(textinput_shift_left_extends_selection) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello");

    // Position cursor at end
    ti->cursor_pos = 5;
    ti->selection_start = 5;
    ti->selection_end = 5;

    vg_event_t ev = make_key_down(VG_KEY_LEFT, VG_MOD_SHIFT);
    bool handled = ti->base.vtable->handle_event(&ti->base, &ev);
    ASSERT_TRUE(handled);
    // Cursor moved left; selection_end updated
    ASSERT_EQ((int)ti->cursor_pos, 4);
    ASSERT_EQ((int)ti->selection_end, 4);
    // Anchor (selection_start) should be unchanged at 5
    ASSERT_EQ((int)ti->selection_start, 5);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — Shift+Right moves the cursor right and extends the selection forward.
TEST(textinput_shift_right_extends_selection) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello");

    ti->cursor_pos = 0;
    ti->selection_start = 0;
    ti->selection_end = 0;

    vg_event_t ev = make_key_down(VG_KEY_RIGHT, VG_MOD_SHIFT);
    ti->base.vtable->handle_event(&ti->base, &ev);
    ASSERT_EQ((int)ti->cursor_pos, 1);
    ASSERT_EQ((int)ti->selection_end, 1);
    ASSERT_EQ((int)ti->selection_start, 0); // anchor stays

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — Shift+Home selects from the current position back to column 0.
TEST(textinput_shift_home_selects_to_start) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello");

    ti->cursor_pos = 5;
    ti->selection_start = 5;
    ti->selection_end = 5;

    vg_event_t ev = make_key_down(VG_KEY_HOME, VG_MOD_SHIFT);
    ti->base.vtable->handle_event(&ti->base, &ev);
    ASSERT_EQ((int)ti->cursor_pos, 0);
    ASSERT_EQ((int)ti->selection_end, 0);
    ASSERT_EQ((int)ti->selection_start, 5); // anchor at original position

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — Shift+End selects from the current position forward to the last character.
TEST(textinput_shift_end_selects_to_end) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello");

    ti->cursor_pos = 2;
    ti->selection_start = 2;
    ti->selection_end = 2;

    vg_event_t ev = make_key_down(VG_KEY_END, VG_MOD_SHIFT);
    ti->base.vtable->handle_event(&ti->base, &ev);
    ASSERT_EQ((int)ti->cursor_pos, 5);
    ASSERT_EQ((int)ti->selection_end, 5);
    ASSERT_EQ((int)ti->selection_start, 2); // anchor stays

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — Ctrl+Right skips the current word and lands at the start of the next word.
TEST(textinput_ctrl_right_jumps_word) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello world");

    ti->cursor_pos = 0;
    ti->selection_start = 0;
    ti->selection_end = 0;

    uint32_t ctrl = VG_MOD_CTRL;
    vg_event_t ev = make_key_down(VG_KEY_RIGHT, ctrl);
    bool handled = ti->base.vtable->handle_event(&ti->base, &ev);
    ASSERT_TRUE(handled);
    // Should skip "hello" and land at the start of "world" (index 6)
    ASSERT_EQ((int)ti->cursor_pos, 6);
    // No shift: selection collapsed
    ASSERT_EQ((int)ti->selection_start, 6);
    ASSERT_EQ((int)ti->selection_end, 6);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — Ctrl+Left skips backward over the current word and lands at its start.
TEST(textinput_ctrl_left_jumps_word) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello world");

    ti->cursor_pos = 11; // end
    ti->selection_start = 11;
    ti->selection_end = 11;

    uint32_t ctrl = VG_MOD_CTRL;
    vg_event_t ev = make_key_down(VG_KEY_LEFT, ctrl);
    bool handled = ti->base.vtable->handle_event(&ti->base, &ev);
    ASSERT_TRUE(handled);
    // Should land at start of "world" (index 6)
    ASSERT_EQ((int)ti->cursor_pos, 6);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — Plain Left with an active selection collapses to the selection start
/// without moving further.
TEST(textinput_plain_left_collapses_selection) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello");

    // Simulate Shift+Right × 3 to build a selection
    ti->cursor_pos = 0;
    ti->selection_start = 0;
    ti->selection_end = 3; // selected "hel"

    vg_event_t ev = make_key_down(VG_KEY_LEFT, VG_MOD_NONE);
    ti->base.vtable->handle_event(&ti->base, &ev);
    // Should collapse to start of selection, not move further left
    ASSERT_EQ((int)ti->cursor_pos, 0);
    ASSERT_EQ((int)ti->selection_start, (int)ti->selection_end);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — Backspace removes the full multi-byte UTF-8 codepoint (€ = 3 bytes), not
/// just one byte.
TEST(textinput_utf8_backspace_removes_whole_codepoint) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti,
                          "A\xE2\x82\xAC"
                          "B");

    ti->cursor_pos = 2;
    ti->selection_start = 2;
    ti->selection_end = 2;

    vg_event_t ev = make_key_down(VG_KEY_BACKSPACE, VG_MOD_NONE);
    ti->base.vtable->handle_event(&ti->base, &ev);
    ASSERT_EQ(strcmp(ti->text, "AB"), 0);
    ASSERT_EQ((int)ti->cursor_pos, 1);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — get_selection on a codepoint-indexed range returns the full UTF-8 byte
/// sequence.
TEST(textinput_utf8_selection_extracts_full_character) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti,
                          "A\xE2\x82\xAC"
                          "B");

    vg_textinput_select(ti, 1, 2);
    char *selection = vg_textinput_get_selection(ti);
    ASSERT_NOT_NULL(selection);
    ASSERT_EQ(strcmp(selection, "\xE2\x82\xAC"), 0);
    free(selection);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — a single-line textinput discards '\n' KEY_CHAR events without modifying
/// text.
TEST(textinput_single_line_ignores_newline_char_input) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "abc");

    vg_event_t ev = make_key_char('\n');
    ASSERT_TRUE(ti->base.vtable->handle_event(&ti->base, &ev));
    ASSERT_EQ(strcmp(ti->text, "abc"), 0);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — max_length counts Unicode codepoints; 3-byte €-characters count as 1 each.
TEST(textinput_max_length_counts_utf8_codepoints) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    ti->max_length = 2;

    vg_textinput_insert(ti, "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC");
    ASSERT_EQ(strcmp(ti->text, "\xE2\x82\xAC\xE2\x82\xAC"), 0);
    ASSERT_EQ((int)ti->cursor_pos, 2);

    vg_widget_destroy(&ti->base);
}

/// @brief BUG-GUI-007 — read-only textinput allows Left to collapse an active selection without
/// modifying text.
TEST(textinput_readonly_navigation_collapses_selection) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);
    vg_textinput_set_text(ti, "hello");
    ti->read_only = true;
    ti->cursor_pos = 4;
    ti->selection_start = 1;
    ti->selection_end = 4;

    vg_event_t ev = make_key_down(VG_KEY_LEFT, VG_MOD_NONE);
    ASSERT_TRUE(ti->base.vtable->handle_event(&ti->base, &ev));
    ASSERT_EQ((int)ti->cursor_pos, 3);
    ASSERT_EQ((int)ti->selection_start, 3);
    ASSERT_EQ((int)ti->selection_end, 3);

    vg_widget_destroy(&ti->base);
}

/// @brief Virtual-mode listbox: MOUSE_DOWN at local y=45 with item_height=20 selects index 2.
TEST(listbox_virtual_mouse_selects_index) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_set_virtual_mode(lb, true, 100, 20.0f);

    lb->base.y = 10.0f;
    lb->base.height = 60.0f;

    vg_event_t ev = make_mouse_down(5.0f, 55.0f);
    ev.mouse.x = 5.0f;
    ev.mouse.y = 45.0f;
    bool handled = lb->base.vtable->handle_event(&lb->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_EQ((int)vg_listbox_get_selected_index(lb), 2);

    vg_widget_destroy(&lb->base);
}

/// @brief Virtual-mode listbox: large wheel delta is clamped to the maximum scroll_y for the item
/// count.
TEST(listbox_virtual_wheel_clamps_using_total_count) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_set_virtual_mode(lb, true, 100, 10.0f);

    lb->base.height = 25.0f;

    vg_event_t ev = make_mouse_wheel(-1000.0f);
    bool handled = lb->base.vtable->handle_event(&lb->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_TRUE(lb->scroll_y > 900.0f);
    ASSERT_TRUE(lb->scroll_y <= 975.0f);

    vg_widget_destroy(&lb->base);
}

//=============================================================================
// Label word_wrap struct fields accessible (BUG-GUI-001 compile check)
//=============================================================================

/// @brief BUG-GUI-001 — label struct exposes word_wrap (default false) and max_lines (default 0).
TEST(label_wordwrap_fields_accessible) {
    vg_label_t *lbl = vg_label_create(NULL, "Hello world this is a test");
    ASSERT_NOT_NULL(lbl);
    // Ensure fields exist and have sane defaults
    ASSERT_FALSE(lbl->word_wrap);
    ASSERT_EQ(lbl->max_lines, 0);

    // Setter smoke test
    lbl->word_wrap = true;
    lbl->max_lines = 3;
    ASSERT_TRUE(lbl->word_wrap);
    ASSERT_EQ(lbl->max_lines, 3);

    vg_widget_destroy(&lbl->base);
}

//=============================================================================
// main
//=============================================================================

/// @brief Run all Tier 1 GUI fix regression tests and report pass/fail counts.
int main(void) {
    printf("=== Tier 1 GUI Fix Tests ===\n");

    printf("\n-- BUG-GUI-003: Button keyboard activation --\n");
    RUN(button_can_focus);
    RUN(button_space_activates);
    RUN(button_enter_activates);
    RUN(button_other_key_does_nothing);
    RUN(button_click_still_works);

    printf("\n-- BUG-GUI-004: Slider focus + keyboard --\n");
    RUN(slider_can_focus);
    RUN(slider_right_key_increases_value);
    RUN(slider_left_key_decreases_value);
    RUN(slider_home_jumps_to_min);
    RUN(slider_end_jumps_to_max);
    RUN(slider_key_respects_step);
    RUN(slider_clamps_at_max);

    printf("\n-- BUG-GUI-005: ListBox focus + keyboard --\n");
    RUN(listbox_can_focus);
    RUN(listbox_down_key_selects_next);
    RUN(listbox_up_key_selects_prev);
    RUN(listbox_home_key_selects_first);
    RUN(listbox_end_key_selects_last);
    RUN(listbox_clamps_at_ends);

    printf("\n-- BUG-GUI-007: TextInput Shift+select + Ctrl+word-jump --\n");
    RUN(textinput_shift_left_extends_selection);
    RUN(textinput_shift_right_extends_selection);
    RUN(textinput_shift_home_selects_to_start);
    RUN(textinput_shift_end_selects_to_end);
    RUN(textinput_ctrl_right_jumps_word);
    RUN(textinput_ctrl_left_jumps_word);
    RUN(textinput_plain_left_collapses_selection);
    RUN(textinput_utf8_backspace_removes_whole_codepoint);
    RUN(textinput_utf8_selection_extracts_full_character);
    RUN(textinput_single_line_ignores_newline_char_input);
    RUN(textinput_max_length_counts_utf8_codepoints);
    RUN(textinput_readonly_navigation_collapses_selection);
    RUN(listbox_virtual_mouse_selects_index);
    RUN(listbox_virtual_wheel_clamps_using_total_count);

    printf("\n-- BUG-GUI-001: Label word_wrap struct fields --\n");
    RUN(label_wordwrap_fields_accessible);

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
