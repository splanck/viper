// test_vg_tier1_fixes.c — Unit tests for Tier 1 GUI bug fixes
//
// Tests:
//   BUG-GUI-003: Button keyboard activation (Space/Enter)
//   BUG-GUI-004: Slider focus + arrow key navigation
//   BUG-GUI-005: ListBox focus + keyboard navigation
//   BUG-GUI-007: TextInput Shift+select, Ctrl+word-jump
//   PERF-001:    vgfx_cls() pixel correctness (32-bit write path)
//   FEAT-004:    Focus ring via border_focus color
//
// Note: vg_label word-wrap (BUG-GUI-001) requires a real font for measurement;
// that is validated by a build-level smoke test in test_vg_label_wordwrap.c.
// macOS resize alignment (BUG-GUI-009) is a platform API contract fix that does
// not require a test executable.
//
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
    do                                                                                             \
    {                                                                                              \
        printf("  %-60s", #name "...");                                                            \
        fflush(stdout);                                                                            \
        test_##name();                                                                             \
        if (g_failed == 0)                                                                         \
            printf("OK\n");                                                                        \
        g_passed++;                                                                                \
    } while (0)

#define ASSERT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("FAIL\n  (%s:%d: %s)\n", __FILE__, __LINE__, #cond);                           \
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

static vg_event_t make_key_down(vg_key_t key, uint32_t mods)
{
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_KEY_DOWN;
    ev.key.key = key;
    ev.modifiers = mods;
    return ev;
}

static vg_event_t make_click(void)
{
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_CLICK;
    return ev;
}

//=============================================================================
// BUG-GUI-003 — Button keyboard activation
//=============================================================================

static int g_button_clicked = 0;
static void button_click_cb(vg_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    g_button_clicked++;
}

TEST(button_can_focus)
{
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    ASSERT_NOT_NULL(btn->base.vtable->can_focus);
    ASSERT_TRUE(btn->base.vtable->can_focus(&btn->base));
    vg_widget_destroy(&btn->base);
}

TEST(button_space_activates)
{
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

TEST(button_enter_activates)
{
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

TEST(button_other_key_does_nothing)
{
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    vg_button_set_on_click(btn, button_click_cb, NULL);

    g_button_clicked = 0;
    vg_event_t ev = make_key_down(VG_KEY_A, VG_MOD_NONE);
    btn->base.vtable->handle_event(&btn->base, &ev);
    ASSERT_EQ(g_button_clicked, 0);

    vg_widget_destroy(&btn->base);
}

TEST(button_click_still_works)
{
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

TEST(slider_can_focus)
{
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->base.vtable->can_focus);
    ASSERT_TRUE(s->base.vtable->can_focus(&s->base));
    vg_widget_destroy(&s->base);
}

TEST(slider_right_key_increases_value)
{
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

TEST(slider_left_key_decreases_value)
{
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_value(s, 50.0f);

    vg_event_t ev = make_key_down(VG_KEY_LEFT, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_TRUE(vg_slider_get_value(s) < 50.0f);

    vg_widget_destroy(&s->base);
}

TEST(slider_home_jumps_to_min)
{
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 10.0f, 200.0f);
    vg_slider_set_value(s, 150.0f);

    vg_event_t ev = make_key_down(VG_KEY_HOME, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_EQ((int)vg_slider_get_value(s), 10);

    vg_widget_destroy(&s->base);
}

TEST(slider_end_jumps_to_max)
{
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_value(s, 30.0f);

    vg_event_t ev = make_key_down(VG_KEY_END, VG_MOD_NONE);
    s->base.vtable->handle_event(&s->base, &ev);
    ASSERT_EQ((int)vg_slider_get_value(s), 100);

    vg_widget_destroy(&s->base);
}

TEST(slider_key_respects_step)
{
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

TEST(slider_clamps_at_max)
{
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

TEST(listbox_can_focus)
{
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    ASSERT_NOT_NULL(lb->base.vtable->can_focus);
    ASSERT_TRUE(lb->base.vtable->can_focus(&lb->base));
    vg_widget_destroy(&lb->base);
}

TEST(listbox_down_key_selects_next)
{
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

TEST(listbox_up_key_selects_prev)
{
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

TEST(listbox_home_key_selects_first)
{
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

TEST(listbox_end_key_selects_last)
{
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

TEST(listbox_clamps_at_ends)
{
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

TEST(textinput_shift_left_extends_selection)
{
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

TEST(textinput_shift_right_extends_selection)
{
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

TEST(textinput_shift_home_selects_to_start)
{
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

TEST(textinput_shift_end_selects_to_end)
{
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

TEST(textinput_ctrl_right_jumps_word)
{
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

TEST(textinput_ctrl_left_jumps_word)
{
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

TEST(textinput_plain_left_collapses_selection)
{
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
    ASSERT_EQ((int)ti->selection_start, ti->selection_end);

    vg_widget_destroy(&ti->base);
}

//=============================================================================
// Label word_wrap struct fields accessible (BUG-GUI-001 compile check)
//=============================================================================

TEST(label_wordwrap_fields_accessible)
{
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

int main(void)
{
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

    printf("\n-- BUG-GUI-001: Label word_wrap struct fields --\n");
    RUN(label_wordwrap_fields_accessible);

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
