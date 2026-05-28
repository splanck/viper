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
#include "rt_bitmapfont.h"
#include "rt_gameui.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_string.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Trap handler for runtime
extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

//=============================================================================
// Test infrastructure
//=============================================================================

static int tests_passed = 0;
static int tests_total = 0;
static int finalizer_calls = 0;

#define TEST(name)                                                                                 \
    do {                                                                                           \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

extern "C" void test_ref_finalizer(void *) {
    finalizer_calls++;
}

static const char *gameui_test_bdf = "STARTFONT 2.1\n"
                                     "FONT -Test-Medium-R-Normal--8-80-75-75-C-80-ISO10646-1\n"
                                     "SIZE 8 75 75\n"
                                     "FONTBOUNDINGBOX 8 8 0 0\n"
                                     "STARTPROPERTIES 2\n"
                                     "FONT_ASCENT 7\n"
                                     "FONT_DESCENT 1\n"
                                     "ENDPROPERTIES\n"
                                     "CHARS 2\n"
                                     "STARTCHAR space\n"
                                     "ENCODING 32\n"
                                     "SWIDTH 500 0\n"
                                     "DWIDTH 8 0\n"
                                     "BBX 8 8 0 0\n"
                                     "BITMAP\n"
                                     "00\n00\n00\n00\n00\n00\n00\n00\n"
                                     "ENDCHAR\n"
                                     "STARTCHAR question\n"
                                     "ENCODING 63\n"
                                     "SWIDTH 500 0\n"
                                     "DWIDTH 8 0\n"
                                     "BBX 8 8 0 0\n"
                                     "BITMAP\n"
                                     "7C\nC6\n0C\n18\n18\n00\n18\n00\n"
                                     "ENDCHAR\n"
                                     "ENDFONT\n";

static void *make_test_font(void) {
    const char *path = "/tmp/viper_gameui_test_font.bdf";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs(gameui_test_bdf, f);
    fclose(f);
    void *font = rt_bitmapfont_load_bdf(rt_const_cstr(path));
    assert(font != NULL);
    return font;
}

//=============================================================================
// UILabel tests
//=============================================================================

static void test_label_creation(void) {
    TEST("UILabel creation and properties");
    rt_string text = rt_const_cstr("Hello");
    void *label = rt_uilabel_new(10, 20, text, 0xFFFFFF);
    assert(label != NULL);
    assert(rt_uilabel_get_x(label) == 10);
    assert(rt_uilabel_get_y(label) == 20);
    PASS();
}

static void test_label_set_pos(void) {
    TEST("UILabel position update");
    rt_string text = rt_const_cstr("Test");
    void *label = rt_uilabel_new(0, 0, text, 0xFFFFFF);
    rt_uilabel_set_pos(label, 50, 100);
    assert(rt_uilabel_get_x(label) == 50);
    assert(rt_uilabel_get_y(label) == 100);
    PASS();
}

static void test_label_null_safety(void) {
    TEST("UILabel NULL safety");
    assert(rt_uilabel_get_x(NULL) == 0);
    assert(rt_uilabel_get_y(NULL) == 0);
    rt_uilabel_set_pos(NULL, 1, 2);   // No crash
    rt_uilabel_set_color(NULL, 0xFF); // No crash
    rt_uilabel_draw(NULL, NULL);      // No crash
    PASS();
}

static void test_label_draw_null_canvas(void) {
    TEST("UILabel draw with NULL canvas is no-op");
    rt_string text = rt_const_cstr("Test");
    void *label = rt_uilabel_new(0, 0, text, 0xFFFFFF);
    rt_uilabel_draw(label, NULL); // Should not crash
    PASS();
}

static void test_label_scale_clamp(void) {
    TEST("UILabel scale clamped to >= 1");
    rt_string text = rt_const_cstr("Test");
    void *label = rt_uilabel_new(0, 0, text, 0xFFFFFF);
    rt_uilabel_set_scale(label, 0);  // Should clamp to 1
    rt_uilabel_set_scale(label, -5); // Should clamp to 1
    // We can't read scale back, but no crash = success
    PASS();
}

static void test_label_retains_font(void) {
    TEST("UILabel retains and releases assigned font");
    finalizer_calls = 0;
    void *font = make_test_font();
    rt_obj_set_finalizer(font, test_ref_finalizer);
    void *label = rt_uilabel_new(0, 0, rt_const_cstr("Font"), 0xFFFFFF);

    rt_uilabel_set_font(label, font);
    release_obj(font);
    assert(finalizer_calls == 0);

    release_obj(label);
    assert(finalizer_calls == 1);
    PASS();
}

//=============================================================================
// UIBar tests
//=============================================================================

static void test_bar_creation(void) {
    TEST("UIBar creation and defaults");
    void *bar = rt_uibar_new(10, 20, 100, 16, 0xFF0000, 0x333333);
    assert(bar != NULL);
    assert(rt_uibar_get_value(bar) == 0);
    assert(rt_uibar_get_max(bar) == 100);
    PASS();
}

static void test_bar_value_clamping(void) {
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

static void test_bar_null_safety(void) {
    TEST("UIBar NULL safety");
    assert(rt_uibar_get_value(NULL) == 0);
    assert(rt_uibar_get_max(NULL) == 0);
    rt_uibar_set_value(NULL, 50, 100); // No crash
    rt_uibar_draw(NULL, NULL);         // No crash
    PASS();
}

static void test_bar_direction(void) {
    TEST("UIBar direction clamping");
    void *bar = rt_uibar_new(0, 0, 100, 16, 0xFF0000, 0x333333);
    rt_uibar_set_direction(bar, 3);  // Valid: top-to-bottom
    rt_uibar_set_direction(bar, 5);  // Invalid → defaults to 0
    rt_uibar_set_direction(bar, -1); // Invalid → defaults to 0
    // No crash = success
    PASS();
}

//=============================================================================
// UIPanel tests
//=============================================================================

static void test_panel_creation(void) {
    TEST("UIPanel creation");
    void *panel = rt_uipanel_new(10, 20, 200, 150, 0x000000, 180);
    assert(panel != NULL);
    PASS();
}

static void test_panel_alpha_clamping(void) {
    TEST("UIPanel alpha clamping");
    void *panel = rt_uipanel_new(0, 0, 100, 100, 0x000000, 300);
    // Alpha should be clamped to 255 internally
    // No getter for alpha, but no crash = success
    rt_uipanel_set_color(panel, 0xFF0000, -50); // Negative → clamp to 0
    PASS();
}

static void test_panel_null_safety(void) {
    TEST("UIPanel NULL safety");
    rt_uipanel_set_pos(NULL, 0, 0);      // No crash
    rt_uipanel_set_size(NULL, 100, 100); // No crash
    rt_uipanel_draw(NULL, NULL);         // No crash
    PASS();
}

//=============================================================================
// UINineSlice tests
//=============================================================================

static void test_nineslice_null_pixels(void) {
    TEST("UINineSlice NULL pixels returns NULL");
    void *ns = rt_uinineslice_new(NULL, 8, 8, 8, 8);
    assert(ns == NULL);
    PASS();
}

static void test_nineslice_draw_null(void) {
    TEST("UINineSlice draw with NULL is no-op");
    rt_uinineslice_draw(NULL, NULL, 0, 0, 100, 100); // No crash
    PASS();
}

static void test_nineslice_retains_pixels(void) {
    TEST("UINineSlice retains and releases Pixels source");
    finalizer_calls = 0;
    void *pixels = rt_pixels_new(3, 3);
    rt_pixels_set(pixels, 1, 1, 0xFFFFFFFF);
    rt_obj_set_finalizer(pixels, test_ref_finalizer);

    void *ns = rt_uinineslice_new(pixels, 1, 1, 1, 1);
    assert(ns != NULL);
    release_obj(pixels);
    assert(finalizer_calls == 0);

    release_obj(ns);
    assert(finalizer_calls == 1);
    PASS();
}

//=============================================================================
// UIMenuList tests
//=============================================================================

static void test_menulist_creation(void) {
    TEST("UIMenuList creation and defaults");
    void *menu = rt_uimenulist_new(10, 20, 24);
    assert(menu != NULL);
    assert(rt_uimenulist_get_count(menu) == 0);
    assert(rt_uimenulist_get_selected(menu) == 0);
    PASS();
}

static void test_menulist_add_items(void) {
    TEST("UIMenuList add items");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("Play"));
    rt_uimenulist_add_item(menu, rt_const_cstr("Options"));
    rt_uimenulist_add_item(menu, rt_const_cstr("Quit"));
    assert(rt_uimenulist_get_count(menu) == 3);
    PASS();
}

static void test_menulist_navigation(void) {
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

static void test_menulist_wrap_down(void) {
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

static void test_menulist_wrap_up(void) {
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

static void test_menulist_empty_navigation(void) {
    TEST("UIMenuList empty list navigation is no-op");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_move_up(menu);   // No crash, no items
    rt_uimenulist_move_down(menu); // No crash
    assert(rt_uimenulist_get_selected(menu) == 0);
    PASS();
}

static void test_menulist_clear(void) {
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

static void test_menulist_set_selected_clamp(void) {
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

static void test_menulist_null_safety(void) {
    TEST("UIMenuList NULL safety");
    assert(rt_uimenulist_get_count(NULL) == 0);
    assert(rt_uimenulist_get_selected(NULL) == 0);
    rt_uimenulist_add_item(NULL, rt_const_cstr("X")); // No crash
    rt_uimenulist_move_up(NULL);                      // No crash
    rt_uimenulist_move_down(NULL);                    // No crash
    rt_uimenulist_draw(NULL, NULL);                   // No crash
    PASS();
}

static void test_menulist_null_text(void) {
    TEST("UIMenuList AddItem with NULL text is no-op");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, NULL);
    assert(rt_uimenulist_get_count(menu) == 0);
    PASS();
}

static void test_menulist_hidden_handle_input_noop(void) {
    TEST("UIMenuList hidden HandleInput is no-op");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));
    rt_uimenulist_set_visible(menu, 0);

    int64_t result = rt_uimenulist_handle_input(menu, 0, 1, 1);
    assert(result == -1);
    assert(rt_uimenulist_get_selected(menu) == 0);
    PASS();
}

static void test_menulist_conflicting_direction_input_noop(void) {
    TEST("UIMenuList HandleInput ignores simultaneous up/down");
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));
    rt_uimenulist_add_item(menu, rt_const_cstr("C"));
    rt_uimenulist_set_selected(menu, 1);

    int64_t result = rt_uimenulist_handle_input(menu, 1, 1, 1);
    assert(result == 1);
    assert(rt_uimenulist_get_selected(menu) == 1);
    PASS();
}

static void test_menulist_retains_font(void) {
    TEST("UIMenuList retains and releases assigned font");
    finalizer_calls = 0;
    void *font = make_test_font();
    rt_obj_set_finalizer(font, test_ref_finalizer);
    void *menu = rt_uimenulist_new(0, 0, 20);

    rt_uimenulist_set_font(menu, font);
    release_obj(font);
    assert(finalizer_calls == 0);

    release_obj(menu);
    assert(finalizer_calls == 1);
    PASS();
}

//=============================================================================
// GameButton tests
//=============================================================================

static void test_gamebutton_creation_and_position(void) {
    TEST("GameButton creation and position properties");
    void *button = rt_gamebutton_new(10, 20, -100, 0, rt_const_cstr("Start"));
    assert(button != NULL);
    assert(rt_gamebutton_get_x(button) == 10);
    assert(rt_gamebutton_get_y(button) == 20);
    assert(rt_gamebutton_get_width(button) == 1);
    assert(rt_gamebutton_get_height(button) == 1);

    rt_gamebutton_set_x(button, 30);
    rt_gamebutton_set_y(button, 40);
    assert(rt_gamebutton_get_x(button) == 30);
    assert(rt_gamebutton_get_y(button) == 40);
    PASS();
}

static void test_gamebutton_size_visible_and_text_scale(void) {
    TEST("GameButton size, visibility, and text scale properties");
    void *button = rt_gamebutton_new(0, 0, 100, 20, rt_const_cstr("Start"));
    assert(rt_gamebutton_get_width(button) == 100);
    assert(rt_gamebutton_get_height(button) == 20);
    assert(rt_gamebutton_get_visible(button) == 1);
    assert(rt_gamebutton_get_text_scale(button) == 1);

    rt_gamebutton_set_size(button, -4, 20000);
    assert(rt_gamebutton_get_width(button) == 1);
    assert(rt_gamebutton_get_height(button) == 16384);

    rt_gamebutton_set_visible(button, 0);
    assert(rt_gamebutton_get_visible(button) == 0);
    rt_gamebutton_set_text_scale(button, 0);
    assert(rt_gamebutton_get_text_scale(button) == 1);
    rt_gamebutton_set_text_scale(button, 20);
    assert(rt_gamebutton_get_text_scale(button) == 16);
    PASS();
}

static void test_gamebutton_null_text_and_border(void) {
    TEST("GameButton NULL text and negative border are safe");
    void *button = rt_gamebutton_new(0, 0, 100, 20, NULL);
    rt_gamebutton_set_text(button, rt_const_cstr("Play"));
    rt_gamebutton_set_text(button, NULL);
    rt_gamebutton_set_border(button, -5, 0xFFFFFF);
    rt_gamebutton_draw(button, NULL, 1);
    PASS();
}

//=============================================================================
// UITextInput / UIDropdown tests
//=============================================================================

static void test_textinput_set_text_stops_at_embedded_nul(void) {
    TEST("UITextInput SetText keeps C-string invariant");
    const char bytes[] = {'A', '\0', 'B'};
    void *input = rt_uitextinput_new(0, 0, 100, 20);
    rt_uitextinput_set_text(input, rt_string_from_bytes(bytes, sizeof(bytes)));

    assert(rt_uitextinput_text_length(input) == 1);
    assert(std::strcmp(rt_string_cstr(rt_uitextinput_get_text(input)), "A") == 0);
    PASS();
}

static void test_textinput_typed_nul_is_ignored(void) {
    TEST("UITextInput typed NUL bytes are ignored");
    const char bytes[] = {'A', '\0', 'B'};
    void *input = rt_uitextinput_new(0, 0, 100, 20);
    rt_uitextinput_set_focused(input, 1);

    assert(rt_uitextinput_handle_text(input, rt_string_from_bytes(bytes, sizeof(bytes))) == 1);
    assert(rt_uitextinput_text_length(input) == 2);
    assert(std::strcmp(rt_string_cstr(rt_uitextinput_get_text(input)), "AB") == 0);
    PASS();
}

static void test_textinput_max_codepoints_truncates_existing_text(void) {
    TEST("UITextInput MaxCodepoints truncates existing text");
    void *input = rt_uitextinput_new(0, 0, 100, 20);
    rt_uitextinput_set_text(input, rt_const_cstr("abcd"));

    rt_uitextinput_set_max_codepoints(input, 2);
    assert(rt_uitextinput_text_length(input) == 2);
    assert(std::strcmp(rt_string_cstr(rt_uitextinput_get_text(input)), "ab") == 0);

    rt_uitextinput_set_max_codepoints(input, 1);
    assert(rt_uitextinput_text_length(input) == 1);
    assert(std::strcmp(rt_string_cstr(rt_uitextinput_get_text(input)), "a") == 0);
    PASS();
}

static void test_dropdown_popup_hit_test_saturates_y(void) {
    TEST("UIDropdown popup hit-test saturates Y coordinate");
    void *dropdown = rt_uidropdown_new(0, INT64_MAX - 5, 20, 10);
    rt_uidropdown_add_option(dropdown, rt_const_cstr("Only"));
    rt_uidropdown_open(dropdown);

    assert(rt_uidropdown_handle_click(dropdown, 1, INT64_MAX) == 1);
    assert(rt_uidropdown_get_selected(dropdown) == 0);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("test_rt_gameui:\n");

    // Label
    test_label_creation();
    test_label_set_pos();
    test_label_null_safety();
    test_label_draw_null_canvas();
    test_label_scale_clamp();
    test_label_retains_font();

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
    test_nineslice_retains_pixels();

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
    test_menulist_hidden_handle_input_noop();
    test_menulist_conflicting_direction_input_noop();
    test_menulist_retains_font();

    // GameButton
    test_gamebutton_creation_and_position();
    test_gamebutton_size_visible_and_text_scale();
    test_gamebutton_null_text_and_border();

    // TextInput / Dropdown
    test_textinput_set_text_stops_at_embedded_nul();
    test_textinput_typed_nul_is_ignored();
    test_textinput_max_codepoints_truncates_existing_text();
    test_dropdown_popup_hit_test_saturates_y();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
