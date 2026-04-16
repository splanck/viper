//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: Tier 2 GUI widget tests — treeview, tabbar, toolbar, statusbar, listbox, output pane.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_tier2_fixes.c
//
//===----------------------------------------------------------------------===//
// test_vg_tier2_fixes.c — Unit tests for Tier 2 GUI improvements
//
// Tests:
//   BINDING-003: GuiWidget field accessors (visible, enabled, flex, layout params)
//   BINDING-004: ScrollView GetScrollX / GetScrollY via vg_scrollview_get_scroll
//   BINDING-006: SplitPane GetPosition via vg_splitpane_get_position
//   PARTIAL-001: CodeEditor gutter_icon_count initially zero, array writable
//   PARTIAL-002: CodeEditor highlight_span_count initially zero, array writable
//   PARTIAL-007: vg_codeeditor_get_selection() returns NULL without selection,
//                non-NULL after vg_codeeditor_set_selection
//   API-005:     vg_widget_set_margin() writes to layout params
//
#include "vg_event.h"
#include "vg_ide_widgets.h"
#include "vg_layout.h"
#include "vg_theme.h"
#include "vg_widget.h"
#include "vg_widgets.h"
#include "vgfx.h"
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

typedef struct {
    vg_widget_t base;
    int handled_count;
} test_bubble_widget_t;

typedef struct {
    vg_widget_t base;
    int mouse_enter_count;
    int mouse_leave_count;
    int key_down_count;
    int paint_count;
    int overlay_count;
} test_probe_widget_t;

static bool test_bubble_handle(vg_widget_t *widget, vg_event_t *event) {
    (void)event;
    ((test_bubble_widget_t *)widget)->handled_count++;
    return true;
}

static const vg_widget_vtable_t g_bubble_vtable = {
    .destroy = NULL,
    .measure = NULL,
    .arrange = NULL,
    .paint = NULL,
    .paint_overlay = NULL,
    .handle_event = test_bubble_handle,
    .can_focus = NULL,
    .on_focus = NULL,
};

static bool test_probe_handle(vg_widget_t *widget, vg_event_t *event) {
    test_probe_widget_t *probe = (test_probe_widget_t *)widget;
    switch (event->type) {
        case VG_EVENT_MOUSE_ENTER:
            probe->mouse_enter_count++;
            return true;
        case VG_EVENT_MOUSE_LEAVE:
            probe->mouse_leave_count++;
            return true;
        case VG_EVENT_KEY_DOWN:
            probe->key_down_count++;
            return true;
        default:
            return false;
    }
}

static void test_probe_paint(vg_widget_t *widget, void *canvas) {
    (void)canvas;
    ((test_probe_widget_t *)widget)->paint_count++;
}

static void test_probe_paint_overlay(vg_widget_t *widget, void *canvas) {
    (void)canvas;
    ((test_probe_widget_t *)widget)->overlay_count++;
}

static const vg_widget_vtable_t g_probe_vtable = {
    .destroy = NULL,
    .measure = NULL,
    .arrange = NULL,
    .paint = test_probe_paint,
    .paint_overlay = NULL,
    .handle_event = test_probe_handle,
    .can_focus = NULL,
    .on_focus = NULL,
};

static const vg_widget_vtable_t g_probe_overlay_vtable = {
    .destroy = NULL,
    .measure = NULL,
    .arrange = NULL,
    .paint = test_probe_paint,
    .paint_overlay = test_probe_paint_overlay,
    .handle_event = test_probe_handle,
    .can_focus = NULL,
    .on_focus = NULL,
};

static test_probe_widget_t *make_probe_widget(const vg_widget_vtable_t *vtable) {
    test_probe_widget_t *probe = calloc(1, sizeof(*probe));
    if (!probe)
        return NULL;
    vg_widget_init(&probe->base, VG_WIDGET_CUSTOM, vtable);
    probe->base.width = 10.0f;
    probe->base.height = 10.0f;
    return probe;
}

static void toolbar_click_counter(vg_toolbar_item_t *item, void *user_data) {
    (void)item;
    (*(int *)user_data)++;
}

static void statusbar_click_counter(vg_statusbar_item_t *item, void *user_data) {
    (void)item;
    (*(int *)user_data)++;
}

//=============================================================================
// BINDING-003: GuiWidget field accessors
//=============================================================================

TEST(widget_is_visible_default_true) {
    vg_label_t *label = vg_label_create(NULL, "hello");
    ASSERT_NOT_NULL(label);
    ASSERT_TRUE(((vg_widget_t *)label)->visible);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_is_visible_after_hide) {
    vg_label_t *label = vg_label_create(NULL, "hi");
    ASSERT_NOT_NULL(label);
    vg_widget_set_visible((vg_widget_t *)label, false);
    ASSERT_FALSE(((vg_widget_t *)label)->visible);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_is_enabled_default_true) {
    vg_button_t *btn = vg_button_create(NULL, "ok");
    ASSERT_NOT_NULL(btn);
    ASSERT_TRUE(((vg_widget_t *)btn)->enabled);
    vg_widget_destroy((vg_widget_t *)btn);
}

TEST(widget_is_enabled_after_disable) {
    vg_button_t *btn = vg_button_create(NULL, "ok");
    ASSERT_NOT_NULL(btn);
    vg_widget_set_enabled((vg_widget_t *)btn, false);
    ASSERT_FALSE(((vg_widget_t *)btn)->enabled);
    vg_widget_destroy((vg_widget_t *)btn);
}

TEST(widget_flex_default_zero) {
    vg_label_t *label = vg_label_create(NULL, "x");
    ASSERT_NOT_NULL(label);
    ASSERT_EQ((int)((vg_widget_t *)label)->layout.flex, 0);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_flex_after_set) {
    vg_label_t *label = vg_label_create(NULL, "x");
    ASSERT_NOT_NULL(label);
    vg_widget_set_flex((vg_widget_t *)label, 2.0f);
    float flex = ((vg_widget_t *)label)->layout.flex;
    ASSERT_TRUE(flex > 1.9f && flex < 2.1f);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_constraints_after_fixed_size) {
    vg_label_t *label = vg_label_create(NULL, "x");
    ASSERT_NOT_NULL(label);
    vg_widget_set_fixed_size((vg_widget_t *)label, 120.0f, 40.0f);
    float pw = ((vg_widget_t *)label)->constraints.preferred_width;
    float ph = ((vg_widget_t *)label)->constraints.preferred_height;
    ASSERT_TRUE(pw >= 119.0f && pw <= 121.0f);
    ASSERT_TRUE(ph >= 39.0f && ph <= 41.0f);
    vg_widget_destroy((vg_widget_t *)label);
}

//=============================================================================
// API-005: SetMargin wires to layout params
//=============================================================================

TEST(widget_set_margin_uniform) {
    vg_label_t *label = vg_label_create(NULL, "x");
    ASSERT_NOT_NULL(label);
    vg_widget_set_margin((vg_widget_t *)label, 8.0f);
    vg_layout_params_t *lp = &((vg_widget_t *)label)->layout;
    ASSERT_TRUE(lp->margin_left > 7.9f && lp->margin_left < 8.1f);
    ASSERT_TRUE(lp->margin_top > 7.9f && lp->margin_top < 8.1f);
    ASSERT_TRUE(lp->margin_right > 7.9f && lp->margin_right < 8.1f);
    ASSERT_TRUE(lp->margin_bottom > 7.9f && lp->margin_bottom < 8.1f);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_set_margin_zero) {
    vg_label_t *label = vg_label_create(NULL, "x");
    ASSERT_NOT_NULL(label);
    vg_widget_set_margin((vg_widget_t *)label, 0.0f);
    vg_layout_params_t *lp = &((vg_widget_t *)label)->layout;
    ASSERT_EQ((int)lp->margin_left, 0);
    ASSERT_EQ((int)lp->margin_top, 0);
    vg_widget_destroy((vg_widget_t *)label);
}

//=============================================================================
// BINDING-004: ScrollView GetScrollX / GetScrollY
//=============================================================================

TEST(scrollview_scroll_defaults_zero) {
    vg_scrollview_t *sv = vg_scrollview_create(NULL);
    ASSERT_NOT_NULL(sv);
    float x = -1.0f, y = -1.0f;
    vg_scrollview_get_scroll(sv, &x, &y);
    ASSERT_EQ((int)x, 0);
    ASSERT_EQ((int)y, 0);
    vg_widget_destroy((vg_widget_t *)sv);
}

TEST(scrollview_scroll_after_set) {
    vg_scrollview_t *sv = vg_scrollview_create(NULL);
    ASSERT_NOT_NULL(sv);
    // Give the widget a viewport and content so clamp_scroll allows non-zero scrolling
    sv->base.width = 100.0f;
    sv->base.height = 100.0f;
    vg_scrollview_set_content_size(sv, 500.0f, 500.0f);
    vg_scrollview_set_scroll(sv, 50.0f, 120.0f);
    float x = 0.0f, y = 0.0f;
    vg_scrollview_get_scroll(sv, &x, &y);
    ASSERT_TRUE(x > 49.0f && x < 51.0f);
    ASSERT_TRUE(y > 119.0f && y < 121.0f);
    vg_widget_destroy((vg_widget_t *)sv);
}

TEST(scrollview_scroll_partial_null_out) {
    // vg_scrollview_get_scroll must accept NULL pointers for either output
    vg_scrollview_t *sv = vg_scrollview_create(NULL);
    ASSERT_NOT_NULL(sv);
    sv->base.width = 100.0f;
    sv->base.height = 100.0f;
    vg_scrollview_set_content_size(sv, 500.0f, 500.0f);
    vg_scrollview_set_scroll(sv, 10.0f, 20.0f);
    float y = 0.0f;
    vg_scrollview_get_scroll(sv, NULL, &y); /* x = NULL should be safe */
    ASSERT_TRUE(y > 19.0f && y < 21.0f);
    vg_widget_destroy((vg_widget_t *)sv);
}

TEST(screen_bounds_ignore_parent_padding) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *parent = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    ASSERT_NOT_NULL(parent);
    ASSERT_NOT_NULL(child);

    vg_widget_add_child(root, parent);
    vg_widget_add_child(parent, child);
    parent->x = 10.0f;
    parent->y = 20.0f;
    parent->layout.padding_left = 7.0f;
    parent->layout.padding_top = 9.0f;
    child->x = 5.0f;
    child->y = 6.0f;
    child->width = 30.0f;
    child->height = 40.0f;

    float sx = 0.0f;
    float sy = 0.0f;
    vg_widget_get_screen_bounds(child, &sx, &sy, NULL, NULL);
    ASSERT_TRUE(sx > 14.9f && sx < 15.1f);
    ASSERT_TRUE(sy > 25.9f && sy < 26.1f);

    vg_widget_destroy(root);
}

TEST(hover_state_tracks_pointer_target) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->width = 200.0f;
    root->height = 100.0f;

    test_probe_widget_t *left = make_probe_widget(&g_probe_vtable);
    test_probe_widget_t *right = make_probe_widget(&g_probe_vtable);
    left->base.width = 40.0f;
    left->base.height = 40.0f;
    right->base.width = 40.0f;
    right->base.height = 40.0f;
    left->base.x = 0.0f;
    left->base.y = 0.0f;
    right->base.x = 50.0f;
    right->base.y = 0.0f;

    vg_widget_add_child(root, &left->base);
    vg_widget_add_child(root, &right->base);

    vg_event_t ev = vg_event_mouse(VG_EVENT_MOUSE_MOVE, 10.0f, 10.0f, VG_MOUSE_LEFT, 0);
    (void)vg_event_dispatch(root, &ev);
    ASSERT_TRUE((left->base.state & VG_STATE_HOVERED) != 0);
    ASSERT_EQ(left->mouse_enter_count, 1);

    ev = vg_event_mouse(VG_EVENT_MOUSE_MOVE, 60.0f, 10.0f, VG_MOUSE_LEFT, 0);
    (void)vg_event_dispatch(root, &ev);
    ASSERT_TRUE((left->base.state & VG_STATE_HOVERED) == 0);
    ASSERT_TRUE((right->base.state & VG_STATE_HOVERED) != 0);
    ASSERT_EQ(left->mouse_leave_count, 1);
    ASSERT_EQ(right->mouse_enter_count, 1);

    ev = vg_event_mouse(VG_EVENT_MOUSE_MOVE, 250.0f, 250.0f, VG_MOUSE_LEFT, 0);
    ASSERT_FALSE(vg_event_dispatch(root, &ev));
    ASSERT_TRUE((right->base.state & VG_STATE_HOVERED) == 0);
    ASSERT_EQ(right->mouse_leave_count, 1);

    vg_widget_destroy(root);
}

TEST(modal_keyboard_defaults_to_modal_root_without_focus) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);

    test_probe_widget_t *background = make_probe_widget(&g_probe_vtable);
    test_probe_widget_t *modal = make_probe_widget(&g_probe_vtable);
    vg_widget_add_child(root, &background->base);
    vg_widget_add_child(root, &modal->base);

    vg_widget_set_focus(NULL);
    vg_widget_set_modal_root(&modal->base);

    vg_event_t ev = vg_event_key(VG_EVENT_KEY_DOWN, VG_KEY_ENTER, 0, 0);
    ASSERT_TRUE(vg_event_dispatch(root, &ev));
    ASSERT_EQ(background->key_down_count, 0);
    ASSERT_EQ(modal->key_down_count, 1);

    vg_widget_set_modal_root(NULL);
    vg_widget_destroy(root);
}

//=============================================================================
// BINDING-006: SplitPane GetPosition
//=============================================================================

TEST(splitpane_get_position_default_in_range) {
    vg_splitpane_t *sp = vg_splitpane_create(NULL, VG_SPLIT_HORIZONTAL);
    ASSERT_NOT_NULL(sp);
    float pos = vg_splitpane_get_position(sp);
    ASSERT_TRUE(pos >= 0.0f && pos <= 1.0f);
    vg_widget_destroy((vg_widget_t *)sp);
}

TEST(splitpane_get_position_after_set) {
    vg_splitpane_t *sp = vg_splitpane_create(NULL, VG_SPLIT_VERTICAL);
    ASSERT_NOT_NULL(sp);
    vg_splitpane_set_position(sp, 0.3f);
    float pos = vg_splitpane_get_position(sp);
    ASSERT_TRUE(pos > 0.29f && pos < 0.31f);
    vg_widget_destroy((vg_widget_t *)sp);
}

TEST(vbox_justify_center_offsets_child) {
    vg_widget_t *vbox = vg_vbox_create(0.0f);
    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(vbox);
    ASSERT_NOT_NULL(child);

    vg_widget_set_fixed_size(child, 10.0f, 10.0f);
    vg_widget_add_child(vbox, child);
    vg_vbox_set_justify(vbox, VG_JUSTIFY_CENTER);
    vg_widget_measure(vbox, 100.0f, 100.0f);
    vg_widget_arrange(vbox, 0.0f, 0.0f, 100.0f, 100.0f);

    ASSERT_TRUE(child->y > 44.9f && child->y < 45.1f);
    vg_widget_destroy(vbox);
}

TEST(hbox_justify_end_offsets_child) {
    vg_widget_t *hbox = vg_hbox_create(0.0f);
    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(hbox);
    ASSERT_NOT_NULL(child);

    vg_widget_set_fixed_size(child, 10.0f, 10.0f);
    vg_widget_add_child(hbox, child);
    vg_hbox_set_justify(hbox, VG_JUSTIFY_END);
    vg_widget_measure(hbox, 100.0f, 40.0f);
    vg_widget_arrange(hbox, 0.0f, 0.0f, 100.0f, 40.0f);

    ASSERT_TRUE(child->x > 89.9f && child->x < 90.1f);
    vg_widget_destroy(hbox);
}

TEST(flex_justify_space_between_adds_gap) {
    vg_widget_t *flex = vg_flex_create();
    vg_widget_t *first = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *second = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(flex);
    ASSERT_NOT_NULL(first);
    ASSERT_NOT_NULL(second);

    vg_widget_set_fixed_size(first, 10.0f, 10.0f);
    vg_widget_set_fixed_size(second, 10.0f, 10.0f);
    vg_widget_add_child(flex, first);
    vg_widget_add_child(flex, second);
    vg_flex_set_direction(flex, VG_DIRECTION_ROW);
    vg_flex_set_justify_content(flex, VG_JUSTIFY_SPACE_BETWEEN);
    vg_widget_measure(flex, 100.0f, 20.0f);
    vg_widget_arrange(flex, 0.0f, 0.0f, 100.0f, 20.0f);

    ASSERT_TRUE(first->x > -0.1f && first->x < 0.1f);
    ASSERT_TRUE(second->x > 89.9f && second->x < 90.1f);
    vg_widget_destroy(flex);
}

TEST(vbox_flex_margins_are_budgeted) {
    vg_widget_t *vbox = vg_vbox_create(0.0f);
    vg_widget_t *flex = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *fixed = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(vbox);
    ASSERT_NOT_NULL(flex);
    ASSERT_NOT_NULL(fixed);

    vg_widget_set_flex(flex, 1.0f);
    flex->layout.margin_top = 10.0f;
    flex->layout.margin_bottom = 10.0f;
    vg_widget_set_fixed_size(fixed, 40.0f, 20.0f);

    vg_widget_add_child(vbox, flex);
    vg_widget_add_child(vbox, fixed);
    vg_widget_measure(vbox, 100.0f, 100.0f);
    vg_widget_arrange(vbox, 0.0f, 0.0f, 100.0f, 100.0f);

    ASSERT_TRUE(flex->height > 59.0f && flex->height < 61.0f);
    ASSERT_TRUE(fixed->y + fixed->height <= 100.1f);

    vg_widget_destroy(vbox);
}

TEST(hbox_flex_margins_are_budgeted) {
    vg_widget_t *hbox = vg_hbox_create(0.0f);
    vg_widget_t *flex = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *fixed = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(hbox);
    ASSERT_NOT_NULL(flex);
    ASSERT_NOT_NULL(fixed);

    vg_widget_set_flex(flex, 1.0f);
    flex->layout.margin_left = 10.0f;
    flex->layout.margin_right = 10.0f;
    vg_widget_set_fixed_size(fixed, 20.0f, 40.0f);

    vg_widget_add_child(hbox, flex);
    vg_widget_add_child(hbox, fixed);
    vg_widget_measure(hbox, 100.0f, 100.0f);
    vg_widget_arrange(hbox, 0.0f, 0.0f, 100.0f, 100.0f);

    ASSERT_TRUE(flex->width > 59.0f && flex->width < 61.0f);
    ASSERT_TRUE(fixed->x + fixed->width <= 100.1f);

    vg_widget_destroy(hbox);
}

//=============================================================================
// PARTIAL-001/002: CodeEditor gutter icon / highlight span arrays
//
// The C-level manipulation functions live in the runtime layer (rt_gui_codeeditor.c)
// and are not part of vipergui.  These tests verify the struct fields are zero-
// initialized on creation — which is the precondition for the rendering code that
// iterates over them (for i < editor->gutter_icon_count / highlight_span_count).
//=============================================================================

TEST(codeeditor_gutter_icon_count_zero_on_create) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(editor);
    ASSERT_EQ(editor->gutter_icon_count, 0);
    ASSERT_NULL(editor->gutter_icons);
    vg_widget_destroy((vg_widget_t *)editor);
}

TEST(codeeditor_highlight_span_count_zero_on_create) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(editor);
    ASSERT_EQ(editor->highlight_span_count, 0);
    ASSERT_NULL(editor->highlight_spans);
    vg_widget_destroy((vg_widget_t *)editor);
}

//=============================================================================
// PARTIAL-007: GetSelectedText binding uses vg_codeeditor_get_selection
//=============================================================================

TEST(codeeditor_get_selection_without_selection_is_null) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(editor);
    vg_codeeditor_set_text(editor, "hello world");
    // has_selection is false — get_selection must return NULL
    char *sel = vg_codeeditor_get_selection(editor);
    ASSERT_NULL(sel);
    vg_widget_destroy((vg_widget_t *)editor);
}

TEST(codeeditor_get_selection_with_selection_returns_text) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(editor);
    vg_codeeditor_set_text(editor, "hello world");
    // Select "hello" (line 0, cols 0–5)
    vg_codeeditor_set_selection(editor, 0, 0, 0, 5);
    char *sel = vg_codeeditor_get_selection(editor);
    ASSERT_NOT_NULL(sel);
    ASSERT_EQ(strncmp(sel, "hello", 5), 0);
    free(sel);
    vg_widget_destroy((vg_widget_t *)editor);
}

//=============================================================================
// TabBar / MenuBar layout regressions
//=============================================================================

TEST(tabbar_metrics_follow_theme_scale) {
    vg_theme_t *previous = vg_theme_get_current();
    vg_theme_t *scaled = vg_theme_create("tier2-scaled", previous);
    ASSERT_NOT_NULL(scaled);

    scaled->ui_scale = 2.0f;
    vg_theme_set_current(scaled);
    vg_tabbar_t *tabbar = vg_tabbar_create(NULL);

    bool ok = tabbar != NULL && tabbar->tab_height > 69.0f && tabbar->tab_height < 71.0f &&
              tabbar->tab_padding > 23.0f && tabbar->tab_padding < 25.0f &&
              tabbar->close_button_size > 27.0f && tabbar->close_button_size < 29.0f &&
              tabbar->max_tab_width > 399.0f && tabbar->max_tab_width < 401.0f;

    if (tabbar)
        vg_widget_destroy((vg_widget_t *)tabbar);
    vg_theme_set_current(previous);
    vg_theme_destroy(scaled);

    ASSERT_TRUE(ok);
}

TEST(tab_tooltip_can_be_replaced) {
    vg_tabbar_t *tabbar = vg_tabbar_create(NULL);
    ASSERT_NOT_NULL(tabbar);

    vg_tab_t *tab = vg_tabbar_add_tab(tabbar, "short.zia", true);
    ASSERT_NOT_NULL(tab);
    ASSERT_NOT_NULL(tab->tooltip);
    ASSERT_TRUE(strcmp(tab->tooltip, "short.zia") == 0);

    vg_tab_set_tooltip(tab, "/tmp/project/short.zia");
    ASSERT_NOT_NULL(tab->tooltip);
    ASSERT_TRUE(strcmp(tab->tooltip, "/tmp/project/short.zia") == 0);

    vg_widget_destroy((vg_widget_t *)tabbar);
}

TEST(tabbar_title_updates_default_tooltip) {
    vg_tabbar_t *tabbar = vg_tabbar_create(NULL);
    ASSERT_NOT_NULL(tabbar);

    vg_tab_t *tab = vg_tabbar_add_tab(tabbar, "old.zia", true);
    ASSERT_NOT_NULL(tab);
    ASSERT_NOT_NULL(tab->tooltip);
    ASSERT_TRUE(strcmp(tab->tooltip, "old.zia") == 0);

    vg_tab_set_title(tab, "new.zia");
    ASSERT_NOT_NULL(tab->tooltip);
    ASSERT_TRUE(strcmp(tab->tooltip, "new.zia") == 0);

    vg_tab_set_tooltip(tab, "/tmp/custom.zia");
    vg_tab_set_title(tab, "newer.zia");
    ASSERT_TRUE(strcmp(tab->tooltip, "/tmp/custom.zia") == 0);

    vg_widget_destroy((vg_widget_t *)tabbar);
}

TEST(tabbar_close_button_requires_full_rect) {
    vg_tabbar_t *tabbar = vg_tabbar_create(NULL);
    ASSERT_NOT_NULL(tabbar);

    vg_tab_t *tab = vg_tabbar_add_tab(tabbar, "tab.zia", true);
    ASSERT_NOT_NULL(tab);

    tabbar->base.x = 0.0f;
    tabbar->base.y = 0.0f;
    tabbar->base.width = 200.0f;
    tabbar->base.height = tabbar->tab_height;

    vg_event_t down = {0};
    down.type = VG_EVENT_MOUSE_DOWN;
    down.mouse.x = 199.0f;
    down.mouse.y = 1.0f;
    down.mouse.screen_x = 199.0f;
    down.mouse.screen_y = 1.0f;

    (void)vg_event_send(&tabbar->base, &down);
    ASSERT_EQ(tabbar->tab_count, 1);
    ASSERT_EQ(tabbar->close_clicked_index, -1);

    vg_widget_destroy((vg_widget_t *)tabbar);
}

TEST(native_menubar_measures_to_zero_height) {
    vg_menubar_t *menubar = vg_menubar_create(NULL);
    ASSERT_NOT_NULL(menubar);

    menubar->native_main_menu = true;
    vg_widget_measure((vg_widget_t *)menubar, 800.0f, 600.0f);

    ASSERT_EQ((int)menubar->base.measured_height, 0);
    ASSERT_EQ((int)menubar->base.constraints.min_height, 0);
    ASSERT_EQ((int)menubar->base.constraints.preferred_height, 0);

    vg_widget_destroy((vg_widget_t *)menubar);
}

TEST(widget_destroy_child_detaches_from_parent) {
    vg_widget_t *parent = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(parent);
    ASSERT_NOT_NULL(child);

    vg_widget_add_child(parent, child);
    ASSERT_EQ(parent->child_count, 1);

    vg_widget_destroy(child);

    ASSERT_EQ(parent->child_count, 0);
    ASSERT_NULL(parent->first_child);
    ASSERT_NULL(parent->last_child);

    vg_widget_destroy(parent);
}

TEST(widget_add_child_rejects_cycles) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    ASSERT_NOT_NULL(child);

    vg_widget_add_child(root, child);
    vg_widget_add_child(child, root);

    ASSERT_NULL(root->parent);
    ASSERT_EQ(root->child_count, 1);
    ASSERT_EQ(root->first_child, child);
    ASSERT_EQ(child->child_count, 0);

    vg_widget_destroy(root);
}

TEST(event_bubble_honors_parent_return_value) {
    test_bubble_widget_t *parent = calloc(1, sizeof(test_bubble_widget_t));
    test_bubble_widget_t *child = calloc(1, sizeof(test_bubble_widget_t));
    ASSERT_NOT_NULL(parent);
    ASSERT_NOT_NULL(child);

    vg_widget_init(&parent->base, VG_WIDGET_CONTAINER, &g_bubble_vtable);
    vg_widget_init(&child->base, VG_WIDGET_CONTAINER, NULL);
    vg_widget_add_child(&parent->base, &child->base);

    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_CLICK;

    bool handled = vg_event_send(&child->base, &ev);
    ASSERT_TRUE(handled);
    ASSERT_TRUE(ev.handled);
    ASSERT_EQ(parent->handled_count, 1);

    vg_widget_destroy(&parent->base);
}

TEST(floatingpanel_destroy_no_double_free) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);

    vg_floatingpanel_t *panel = vg_floatingpanel_create(root);
    ASSERT_NOT_NULL(panel);

    vg_widget_t *child = (vg_widget_t *)vg_label_create(NULL, "child");
    ASSERT_NOT_NULL(child);
    vg_floatingpanel_add_child(panel, child);
    ASSERT_EQ(child->parent, &panel->base);
    ASSERT_EQ(panel->base.child_count, 1);

    vg_floatingpanel_destroy(panel);
    ASSERT_EQ(root->child_count, 0);

    vg_widget_destroy(root);
}

TEST(scrollview_auto_content_size_uses_measured_children) {
    vg_scrollview_t *sv = vg_scrollview_create(NULL);
    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(sv);
    ASSERT_NOT_NULL(child);

    vg_widget_set_fixed_size(child, 60.0f, 20.0f);
    child->layout.margin_left = 12.0f;
    child->layout.margin_top = 8.0f;
    vg_widget_add_child(&sv->base, child);

    vg_widget_arrange(&sv->base, 0.0f, 0.0f, 100.0f, 100.0f);
    ASSERT_TRUE(sv->content_width > 71.9f && sv->content_width < 72.1f);
    ASSERT_TRUE(sv->content_height > 27.9f && sv->content_height < 28.1f);

    vg_widget_destroy(&sv->base);
}

TEST(scrollview_auto_hide_rechecks_cross_axis) {
    vg_scrollview_t *sv = vg_scrollview_create(NULL);
    ASSERT_NOT_NULL(sv);

    vg_scrollview_set_content_size(sv, 95.0f, 200.0f);
    vg_widget_arrange(&sv->base, 0.0f, 0.0f, 100.0f, 100.0f);

    ASSERT_TRUE(sv->show_v_scrollbar);
    ASSERT_TRUE(sv->show_h_scrollbar);

    vg_widget_destroy(&sv->base);
}

TEST(scrollview_scroll_to_nested_descendant_uses_full_offset) {
    vg_scrollview_t *sv = vg_scrollview_create(NULL);
    vg_widget_t *container = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(sv);
    ASSERT_NOT_NULL(container);
    ASSERT_NOT_NULL(child);

    vg_widget_add_child(&sv->base, container);
    vg_widget_add_child(container, child);
    sv->base.width = 100.0f;
    sv->base.height = 100.0f;
    sv->content_width = 200.0f;
    sv->content_height = 100.0f;
    sv->show_h_scrollbar = false;
    sv->show_v_scrollbar = false;
    container->x = 90.0f;
    child->x = 30.0f;
    child->width = 30.0f;
    vg_scrollview_scroll_to_widget(sv, child);
    ASSERT_TRUE(sv->scroll_x > 49.9f && sv->scroll_x < 50.1f);

    vg_widget_destroy(&sv->base);
}

TEST(widget_paint_runs_overlay_second_pass) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    test_probe_widget_t *overlay = make_probe_widget(&g_probe_overlay_vtable);
    ASSERT_NOT_NULL(root);
    vg_widget_add_child(root, &overlay->base);

    vg_widget_paint(root, (void *)0x1);
    ASSERT_EQ(overlay->paint_count, 1);
    ASSERT_EQ(overlay->overlay_count, 1);

    vg_widget_destroy(root);
}

TEST(widget_set_focus_null_clears_focus) {
    test_probe_widget_t *probe = make_probe_widget(&g_probe_vtable);
    ASSERT_NOT_NULL(probe);
    probe->base.enabled = true;
    probe->base.visible = true;

    vg_widget_set_focus(&probe->base);
    ASSERT_TRUE((probe->base.state & VG_STATE_FOCUSED) != 0);
    vg_widget_set_focus(NULL);
    ASSERT_TRUE((probe->base.state & VG_STATE_FOCUSED) == 0);

    vg_widget_destroy(&probe->base);
}

TEST(splitpane_arrange_clamps_negative_sizes) {
    vg_splitpane_t *split = vg_splitpane_create(NULL, VG_SPLIT_HORIZONTAL);
    ASSERT_NOT_NULL(split);

    vg_splitpane_set_min_sizes(split, 50.0f, 50.0f);
    vg_widget_arrange(&split->base, 0.0f, 0.0f, 3.0f, 40.0f);

    vg_widget_t *first = split->base.first_child;
    vg_widget_t *second = first ? first->next_sibling : NULL;
    ASSERT_NOT_NULL(first);
    ASSERT_NOT_NULL(second);
    ASSERT_TRUE(first->width >= 0.0f);
    ASSERT_TRUE(second->width >= 0.0f);

    vg_widget_destroy(&split->base);
}

TEST(toolbar_hit_testing_uses_local_coords) {
    int click_count = 0;
    vg_toolbar_t *tb = vg_toolbar_create(NULL, VG_TOOLBAR_HORIZONTAL);
    ASSERT_NOT_NULL(tb);

    vg_toolbar_item_t *item = vg_toolbar_add_button(
        tb, "open", NULL, vg_icon_from_glyph('O'), toolbar_click_counter, &click_count);
    ASSERT_NOT_NULL(item);
    vg_widget_arrange(&tb->base, 100.0f, 50.0f, 80.0f, 32.0f);

    vg_event_t down = {0};
    down.type = VG_EVENT_MOUSE_DOWN;
    down.mouse.x = 10.0f;
    down.mouse.y = 10.0f;
    down.mouse.screen_x = 110.0f;
    down.mouse.screen_y = 60.0f;
    ASSERT_TRUE(vg_event_send(&tb->base, &down));
    ASSERT_EQ(tb->pressed_item, item);

    vg_event_t up = down;
    up.type = VG_EVENT_MOUSE_UP;
    ASSERT_TRUE(vg_event_send(&tb->base, &up));
    ASSERT_EQ(click_count, 1);

    vg_widget_destroy(&tb->base);
}

TEST(toolbar_overflow_popup_exposes_hidden_items) {
    int hidden_clicks = 0;
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->width = 320.0f;
    root->height = 240.0f;

    vg_toolbar_t *tb = vg_toolbar_create(root, VG_TOOLBAR_HORIZONTAL);
    ASSERT_NOT_NULL(tb);
    ASSERT_NOT_NULL(vg_toolbar_add_button(
        tb, "a", NULL, vg_icon_from_glyph('A'), toolbar_click_counter, &hidden_clicks));
    ASSERT_NOT_NULL(vg_toolbar_add_button(
        tb, "b", NULL, vg_icon_from_glyph('B'), toolbar_click_counter, &hidden_clicks));
    ASSERT_NOT_NULL(vg_toolbar_add_button(
        tb, "c", NULL, vg_icon_from_glyph('C'), toolbar_click_counter, &hidden_clicks));

    vg_widget_arrange(&tb->base, 0.0f, 0.0f, 80.0f, 32.0f);
    ASSERT_TRUE(tb->overflow_start_index >= 0);

    vg_event_t click_overflow =
        vg_event_mouse(VG_EVENT_MOUSE_DOWN, 75.0f, 10.0f, VG_MOUSE_LEFT, 0);
    ASSERT_TRUE(vg_event_dispatch(root, &click_overflow));
    ASSERT_NOT_NULL(tb->overflow_popup);
    ASSERT_TRUE(tb->overflow_popup->is_visible);

    vg_event_t click_popup = vg_event_mouse(VG_EVENT_MOUSE_DOWN,
                                            tb->overflow_popup->base.x + 5.0f,
                                            tb->overflow_popup->base.y + 10.0f,
                                            VG_MOUSE_LEFT,
                                            0);
    ASSERT_TRUE(vg_event_dispatch(root, &click_popup));
    ASSERT_EQ(hidden_clicks, 1);
    ASSERT_FALSE(tb->overflow_popup->is_visible);

    vg_widget_destroy(root);
}

TEST(statusbar_hit_testing_uses_local_coords) {
    int click_count = 0;
    vg_statusbar_t *sb = vg_statusbar_create(NULL);
    ASSERT_NOT_NULL(sb);
    ASSERT_NOT_NULL(
        vg_statusbar_add_button(sb, VG_STATUSBAR_ZONE_LEFT, "Build", statusbar_click_counter, &click_count));
    vg_widget_arrange(&sb->base, 200.0f, 50.0f, 240.0f, (float)sb->height);

    vg_event_t click = {0};
    click.type = VG_EVENT_CLICK;
    click.mouse.x = sb->item_padding + 5.0f;
    click.mouse.y = 5.0f;
    click.mouse.screen_x = sb->base.x + click.mouse.x;
    click.mouse.screen_y = sb->base.y + click.mouse.y;
    ASSERT_TRUE(vg_event_send(&sb->base, &click));
    ASSERT_EQ(click_count, 1);

    vg_widget_destroy(&sb->base);
}

TEST(listbox_virtual_paint_clips_to_viewport) {
    vgfx_window_params_t params = {
        .width = 100, .height = 100, .title = "listbox", .fps = 0, .resizable = 0};
    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);
    vgfx_cls(win, VGFX_BLACK);

    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    lb->bg_color = VGFX_BLACK;
    lb->item_bg = 0xFF0000;
    lb->selected_bg = 0x00FF00;
    lb->hover_bg = 0x0000FF;
    vg_listbox_set_virtual_mode(lb, true, 10, 20.0f);
    lb->scroll_y = 10.0f;
    vg_widget_arrange(&lb->base, 20.0f, 20.0f, 50.0f, 50.0f);

    vg_widget_paint(&lb->base, win);

    vgfx_color_t color = 0;
    ASSERT_TRUE(vgfx_point(win, 25, 15, &color) == 1);
    ASSERT_EQ(color, VGFX_BLACK);
    ASSERT_TRUE(vgfx_point(win, 25, 25, &color) == 1);
    ASSERT_EQ(color, 0xFF0000);

    vg_widget_destroy(&lb->base);
    vgfx_destroy_window(win);
}

//=============================================================================
// Main
//=============================================================================

int main(void) {
    printf("=== test_vg_tier2_fixes ===\n");

    // BINDING-003: Widget field accessors
    RUN(widget_is_visible_default_true);
    RUN(widget_is_visible_after_hide);
    RUN(widget_is_enabled_default_true);
    RUN(widget_is_enabled_after_disable);
    RUN(widget_flex_default_zero);
    RUN(widget_flex_after_set);
    RUN(widget_constraints_after_fixed_size);

    // API-005: SetMargin
    RUN(widget_set_margin_uniform);
    RUN(widget_set_margin_zero);

    // BINDING-004: ScrollView
    RUN(scrollview_scroll_defaults_zero);
    RUN(scrollview_scroll_after_set);
    RUN(scrollview_scroll_partial_null_out);
    RUN(screen_bounds_ignore_parent_padding);
    RUN(hover_state_tracks_pointer_target);
    RUN(modal_keyboard_defaults_to_modal_root_without_focus);

    // BINDING-006: SplitPane
    RUN(splitpane_get_position_default_in_range);
    RUN(splitpane_get_position_after_set);
    RUN(vbox_justify_center_offsets_child);
    RUN(hbox_justify_end_offsets_child);
    RUN(flex_justify_space_between_adds_gap);
    RUN(vbox_flex_margins_are_budgeted);
    RUN(hbox_flex_margins_are_budgeted);

    // PARTIAL-001/002: CodeEditor arrays zero on create
    RUN(codeeditor_gutter_icon_count_zero_on_create);
    RUN(codeeditor_highlight_span_count_zero_on_create);

    // PARTIAL-007: GetSelectedText
    RUN(codeeditor_get_selection_without_selection_is_null);
    RUN(codeeditor_get_selection_with_selection_returns_text);
    RUN(tabbar_metrics_follow_theme_scale);
    RUN(tab_tooltip_can_be_replaced);
    RUN(tabbar_title_updates_default_tooltip);
    RUN(tabbar_close_button_requires_full_rect);
    RUN(native_menubar_measures_to_zero_height);
    RUN(widget_destroy_child_detaches_from_parent);
    RUN(widget_add_child_rejects_cycles);
    RUN(event_bubble_honors_parent_return_value);
    RUN(floatingpanel_destroy_no_double_free);
    RUN(scrollview_auto_content_size_uses_measured_children);
    RUN(scrollview_auto_hide_rechecks_cross_axis);
    RUN(scrollview_scroll_to_nested_descendant_uses_full_offset);
    RUN(widget_paint_runs_overlay_second_pass);
    RUN(widget_set_focus_null_clears_focus);
    RUN(splitpane_arrange_clamps_negative_sizes);
    RUN(toolbar_hit_testing_uses_local_coords);
    RUN(toolbar_overflow_popup_exposes_hidden_items);
    RUN(statusbar_hit_testing_uses_local_coords);
    RUN(listbox_virtual_paint_clips_to_viewport);

    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
