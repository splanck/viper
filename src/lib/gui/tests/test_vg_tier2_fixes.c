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
// BINDING-003: GuiWidget field accessors
//=============================================================================

TEST(widget_is_visible_default_true)
{
    vg_label_t *label = vg_label_create(NULL, "hello");
    ASSERT_NOT_NULL(label);
    ASSERT_TRUE(((vg_widget_t *)label)->visible);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_is_visible_after_hide)
{
    vg_label_t *label = vg_label_create(NULL, "hi");
    ASSERT_NOT_NULL(label);
    vg_widget_set_visible((vg_widget_t *)label, false);
    ASSERT_FALSE(((vg_widget_t *)label)->visible);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_is_enabled_default_true)
{
    vg_button_t *btn = vg_button_create(NULL, "ok");
    ASSERT_NOT_NULL(btn);
    ASSERT_TRUE(((vg_widget_t *)btn)->enabled);
    vg_widget_destroy((vg_widget_t *)btn);
}

TEST(widget_is_enabled_after_disable)
{
    vg_button_t *btn = vg_button_create(NULL, "ok");
    ASSERT_NOT_NULL(btn);
    vg_widget_set_enabled((vg_widget_t *)btn, false);
    ASSERT_FALSE(((vg_widget_t *)btn)->enabled);
    vg_widget_destroy((vg_widget_t *)btn);
}

TEST(widget_flex_default_zero)
{
    vg_label_t *label = vg_label_create(NULL, "x");
    ASSERT_NOT_NULL(label);
    ASSERT_EQ((int)((vg_widget_t *)label)->layout.flex, 0);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_flex_after_set)
{
    vg_label_t *label = vg_label_create(NULL, "x");
    ASSERT_NOT_NULL(label);
    vg_widget_set_flex((vg_widget_t *)label, 2.0f);
    float flex = ((vg_widget_t *)label)->layout.flex;
    ASSERT_TRUE(flex > 1.9f && flex < 2.1f);
    vg_widget_destroy((vg_widget_t *)label);
}

TEST(widget_constraints_after_fixed_size)
{
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

TEST(widget_set_margin_uniform)
{
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

TEST(widget_set_margin_zero)
{
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

TEST(scrollview_scroll_defaults_zero)
{
    vg_scrollview_t *sv = vg_scrollview_create(NULL);
    ASSERT_NOT_NULL(sv);
    float x = -1.0f, y = -1.0f;
    vg_scrollview_get_scroll(sv, &x, &y);
    ASSERT_EQ((int)x, 0);
    ASSERT_EQ((int)y, 0);
    vg_widget_destroy((vg_widget_t *)sv);
}

TEST(scrollview_scroll_after_set)
{
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

TEST(scrollview_scroll_partial_null_out)
{
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

//=============================================================================
// BINDING-006: SplitPane GetPosition
//=============================================================================

TEST(splitpane_get_position_default_in_range)
{
    vg_splitpane_t *sp = vg_splitpane_create(NULL, VG_SPLIT_HORIZONTAL);
    ASSERT_NOT_NULL(sp);
    float pos = vg_splitpane_get_position(sp);
    ASSERT_TRUE(pos >= 0.0f && pos <= 1.0f);
    vg_widget_destroy((vg_widget_t *)sp);
}

TEST(splitpane_get_position_after_set)
{
    vg_splitpane_t *sp = vg_splitpane_create(NULL, VG_SPLIT_VERTICAL);
    ASSERT_NOT_NULL(sp);
    vg_splitpane_set_position(sp, 0.3f);
    float pos = vg_splitpane_get_position(sp);
    ASSERT_TRUE(pos > 0.29f && pos < 0.31f);
    vg_widget_destroy((vg_widget_t *)sp);
}

//=============================================================================
// PARTIAL-001/002: CodeEditor gutter icon / highlight span arrays
//
// The C-level manipulation functions live in the runtime layer (rt_gui_codeeditor.c)
// and are not part of vipergui.  These tests verify the struct fields are zero-
// initialized on creation — which is the precondition for the rendering code that
// iterates over them (for i < editor->gutter_icon_count / highlight_span_count).
//=============================================================================

TEST(codeeditor_gutter_icon_count_zero_on_create)
{
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(editor);
    ASSERT_EQ(editor->gutter_icon_count, 0);
    ASSERT_NULL(editor->gutter_icons);
    vg_widget_destroy((vg_widget_t *)editor);
}

TEST(codeeditor_highlight_span_count_zero_on_create)
{
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(editor);
    ASSERT_EQ(editor->highlight_span_count, 0);
    ASSERT_NULL(editor->highlight_spans);
    vg_widget_destroy((vg_widget_t *)editor);
}

//=============================================================================
// PARTIAL-007: GetSelectedText binding uses vg_codeeditor_get_selection
//=============================================================================

TEST(codeeditor_get_selection_without_selection_is_null)
{
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(editor);
    vg_codeeditor_set_text(editor, "hello world");
    // has_selection is false — get_selection must return NULL
    char *sel = vg_codeeditor_get_selection(editor);
    ASSERT_NULL(sel);
    vg_widget_destroy((vg_widget_t *)editor);
}

TEST(codeeditor_get_selection_with_selection_returns_text)
{
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
// Main
//=============================================================================

int main(void)
{
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

    // BINDING-006: SplitPane
    RUN(splitpane_get_position_default_in_range);
    RUN(splitpane_get_position_after_set);

    // PARTIAL-001/002: CodeEditor arrays zero on create
    RUN(codeeditor_gutter_icon_count_zero_on_create);
    RUN(codeeditor_highlight_span_count_zero_on_create);

    // PARTIAL-007: GetSelectedText
    RUN(codeeditor_get_selection_without_selection_is_null);
    RUN(codeeditor_get_selection_with_selection_returns_text);

    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
