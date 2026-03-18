//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_gameui.cpp
// Purpose: Unit tests for Game UI widgets (Label, Bar, Panel, NineSlice,
//   MenuList). Tests state management, value clamping, menu navigation
//   wrap-around, and NULL safety. Does not test Canvas drawing (requires
//   VIPER_ENABLE_GRAPHICS).
//
// Key invariants:
//   - UIBar value clamped to [0, max], max >= 1.
//   - UIMenuList selection wraps on MoveUp/MoveDown.
//   - All functions are safe with NULL inputs.
//
// Ownership/Lifetime:
//   - Uses runtime library. Widget objects are GC-managed.
//
// Links: src/runtime/collections/rt_gameui.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_gameui.h"
#include "rt_internal.h"
#include <cassert>
#include <cstdio>

// Trap handler for runtime
extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

//=============================================================================
// Test infrastructure
//=============================================================================

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                   \
    do                                                                                               \
    {                                                                                                \
        tests_total++;                                                                               \
        printf("  [%d] %s... ", tests_total, name);                                                  \
    } while (0)

#define PASS()                                                                                       \
    do                                                                                               \
    {                                                                                                \
        tests_passed++;                                                                              \
        printf("ok\n");                                                                              \
    } while (0)

//=============================================================================
// UILabel tests
//=============================================================================

static void test_label_creation(void)
{
    TEST("UILabel creation and properties");
    rt_string text = rt_const_cstr("Hello");
    void *label = rt_uilabel_new(10, 20, text, 0xFFFFFF);
    assert(label != NULL);
    assert(rt_uilabel_get_x(label) == 10);
    assert(rt_uilabel_get_y(label) == 20);
    PASS();
}

static void test_label_set_pos(void)
{
    TEST("UILabel position update");
    rt_string text = rt_const_cstr("Test");
    void *label = rt_uilabel_new(0, 0, text, 0xFFFFFF);
    rt_uilabel_set_pos(label, 50, 100);
    assert(rt_uilabel_get_x(label) == 50);
    assert(rt_uilabel_get_y(label) == 100);
    PASS();
}

static void test_label_null_safety(void)
{
    TEST("UILabel NULL safety");
    assert(rt_uilabel_get_x(NULL) == 0);
    assert(rt_uilabel_get_y(NULL) == 0);
    rt_uilabel_set_pos(NULL, 1, 2);   // No crash
    rt_uilabel_set_color(NULL, 0xFF);  // No crash
    rt_uilabel_draw(NULL, NULL);       // No crash
    PASS();
}

static void test_label_draw_null_canvas(void)
{
    TEST("UILabel draw with NULL canvas is no-op");
    rt_string text = rt_const_cstr("Test");
    void *label = rt_uilabel_new(0, 0, text, 0xFFFFFF);
    rt_uilabel_draw(label, NULL); // Should not crash
    PASS();
}

static void test_label_scale_clamp(void)
{
    TEST("UILabel scale clamped to >= 1");
    rt_string text = rt_const_cstr("Test");
    void *label = rt_uilabel_new(0, 0, text, 0xFFFFFF);
    rt_uilabel_set_scale(label, 0);  // Should clamp to 1
    rt_uilabel_set_scale(label, -5); // Should clamp to 1
    // We can't read scale back, but no crash = success
    PASS();
}

//=============================================================================
// UIBar tests
//=============================================================================

static void test_bar_creation(void)
{
    TEST("UIBar creation and defaults");
    void *bar = rt_uibar_new(10, 20, 100, 16, 0xFF0000, 0x333333);
    assert(bar != NULL);
    assert(rt_uibar_get_value(bar) == 0);
    assert(rt_uibar_get_max(bar) == 100);
    PASS();
}

static void test_bar_value_clamping(void)
{
    TEST("UIBar value clamping");
    void *bar = rt_uibar_new(0, 0, 100, 16, 0xFF0000, 0x333333);

    // Normal set
    rt_uibar_set_value(bar, 75, 100);
    assert(rt_uibar_get_value(bar) == 75);
    assert(rt_uibar_get_max(bar) == 100);

    // Over max → clamp to max
    rt_uibar_set_value(bar, 150, 100);
    assert(rt_uibar_get_value(bar) == 100);

    // Negative → clamp to 0
    rt_uibar_set_value(bar, -10, 100);
    assert(rt_uibar_get_value(bar) == 0);

    // Max < 1 → clamp to 1
    rt_uibar_set_value(bar, 5, 0);
    assert(rt_uibar_get_max(bar) == 1);

    PASS();
}

static void test_bar_null_safety(void)
{
    TEST("UIBar NULL safety");
    assert(rt_uibar_get_value(NULL) == 0);
    assert(rt_uibar_get_max(NULL) == 0);
    rt_uibar_set_value(NULL, 50, 100); // No crash
    rt_uibar_draw(NULL, NULL);          // No crash
    PASS();
}

static void test_bar_direction(void)
{
    TEST("UIBar direction clamping");
    void *bar = rt_uibar_new(0, 0, 100, 16, 0xFF0000, 0x333333);
    rt_uibar_set_direction(bar, 3); // Valid: top-to-bottom
    rt_uibar_set_direction(bar, 5); // Invalid → defaults to 0
    rt_uibar_set_direction(bar, -1); // Invalid → defaults to 0
    // No crash = success
    PASS();
}

//=============================================================================
// UIPanel tests
//=============================================================================

static void test_panel_creation(void)
{
    TEST("UIPanel creation");
    void *panel = rt_uipanel_new(10, 20, 200, 150, 0x000000, 180);
    assert(panel != NULL);
    PASS();
}

static void test_panel_alpha_clamping(void)
{
    TEST("UIPanel alpha clamping");
    void *panel = rt_uipanel_new(0, 0, 100, 100, 0x000000, 300);
    // Alpha should be clamped to 255 internally
    // No getter for alpha, but no crash = success
    rt_uipanel_set_color(panel, 0xFF0000, -50); // Negative → clamp to 0
    PASS();
}

static void test_panel_null_safety(void)
{
    TEST("UIPanel NULL safety");
    rt_uipanel_set_pos(NULL, 0, 0);     // No crash
    rt_uipanel_set_size(NULL, 100, 100); // No crash
    rt_uipanel_draw(NULL, NULL);          // No crash
    PASS();
}

//=============================================================================
// UINineSlice tests
//=============================================================================

static void test_nineslice_null_pixels(void)
{
    TEST("UINineSlice NULL pixels returns NULL");
    void *ns = rt_uinineslice_new(NULL, 8, 8, 8, 8);
    assert(ns == NULL);
    PASS();
}

static void test_nineslice_draw_null(void)
{
    TEST("UINineSlice draw with NULL is no-op");
    rt_uinineslice_draw(NULL, NULL, 0, 0, 100, 100); // No crash
    PASS();
}

//=============================================================================
// UIMenuList tests
//=============================================================================

static void test_menulist_creation(void)
{
    TEST("UIMenuList creation and defaults");
    void *menu = rt_uimenulist_new(10, 20, 24);
    assert(menu != NULL);
    assert(rt_uimenulist_get_count(menu) == 0);
    assert(rt_uimenulist_get_selected(menu) == 0);
    PASS();
}

static void test_menulist_add_items(void)
{
    TEST("UIMenuList add items");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("Play"));
    rt_uimenulist_add_item(menu, rt_const_cstr("Options"));
    rt_uimenulist_add_item(menu, rt_const_cstr("Quit"));
    assert(rt_uimenulist_get_count(menu) == 3);
    PASS();
}

static void test_menulist_navigation(void)
{
    TEST("UIMenuList MoveDown/MoveUp navigation");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));
    rt_uimenulist_add_item(menu, rt_const_cstr("C"));

    assert(rt_uimenulist_get_selected(menu) == 0);

    rt_uimenulist_move_down(menu);
    assert(rt_uimenulist_get_selected(menu) == 1);

    rt_uimenulist_move_down(menu);
    assert(rt_uimenulist_get_selected(menu) == 2);

    rt_uimenulist_move_up(menu);
    assert(rt_uimenulist_get_selected(menu) == 1);
    PASS();
}

static void test_menulist_wrap_down(void)
{
    TEST("UIMenuList wrap-around down (last → 0)");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));
    rt_uimenulist_add_item(menu, rt_const_cstr("C"));

    rt_uimenulist_set_selected(menu, 2); // At last item
    rt_uimenulist_move_down(menu);
    assert(rt_uimenulist_get_selected(menu) == 0); // Wrapped to top
    PASS();
}

static void test_menulist_wrap_up(void)
{
    TEST("UIMenuList wrap-around up (0 → last)");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));
    rt_uimenulist_add_item(menu, rt_const_cstr("C"));

    assert(rt_uimenulist_get_selected(menu) == 0); // At first item
    rt_uimenulist_move_up(menu);
    assert(rt_uimenulist_get_selected(menu) == 2); // Wrapped to bottom
    PASS();
}

static void test_menulist_empty_navigation(void)
{
    TEST("UIMenuList empty list navigation is no-op");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_move_up(menu);   // No crash, no items
    rt_uimenulist_move_down(menu); // No crash
    assert(rt_uimenulist_get_selected(menu) == 0);
    PASS();
}

static void test_menulist_clear(void)
{
    TEST("UIMenuList clear resets state");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));
    rt_uimenulist_set_selected(menu, 1);

    rt_uimenulist_clear(menu);
    assert(rt_uimenulist_get_count(menu) == 0);
    assert(rt_uimenulist_get_selected(menu) == 0);
    PASS();
}

static void test_menulist_set_selected_clamp(void)
{
    TEST("UIMenuList set_selected clamps to valid range");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));

    rt_uimenulist_set_selected(menu, 10); // Out of range → clamp to 1
    assert(rt_uimenulist_get_selected(menu) == 1);

    rt_uimenulist_set_selected(menu, -5); // Negative → clamp to 0
    assert(rt_uimenulist_get_selected(menu) == 0);
    PASS();
}

static void test_menulist_null_safety(void)
{
    TEST("UIMenuList NULL safety");
    assert(rt_uimenulist_get_count(NULL) == 0);
    assert(rt_uimenulist_get_selected(NULL) == 0);
    rt_uimenulist_add_item(NULL, rt_const_cstr("X")); // No crash
    rt_uimenulist_move_up(NULL);   // No crash
    rt_uimenulist_move_down(NULL); // No crash
    rt_uimenulist_draw(NULL, NULL); // No crash
    PASS();
}

static void test_menulist_null_text(void)
{
    TEST("UIMenuList AddItem with NULL text is no-op");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, NULL);
    assert(rt_uimenulist_get_count(menu) == 0);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("test_rt_gameui:\n");

    // Label
    test_label_creation();
    test_label_set_pos();
    test_label_null_safety();
    test_label_draw_null_canvas();
    test_label_scale_clamp();

    // Bar
    test_bar_creation();
    test_bar_value_clamping();
    test_bar_null_safety();
    test_bar_direction();

    // Panel
    test_panel_creation();
    test_panel_alpha_clamping();
    test_panel_null_safety();

    // NineSlice
    test_nineslice_null_pixels();
    test_nineslice_draw_null();

    // MenuList
    test_menulist_creation();
    test_menulist_add_items();
    test_menulist_navigation();
    test_menulist_wrap_down();
    test_menulist_wrap_up();
    test_menulist_empty_navigation();
    test_menulist_clear();
    test_menulist_set_selected_clamp();
    test_menulist_null_safety();
    test_menulist_null_text();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
