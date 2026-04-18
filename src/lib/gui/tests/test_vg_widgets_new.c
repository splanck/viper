//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: Tests for newly added GUI widgets — notification, breadcrumb, minimap, floating panel.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_widgets_new.c
//
//===----------------------------------------------------------------------===//
// test_vg_widgets_new.c — Unit tests for new widget features
// Tests slider vtable, progressbar vtable, listbox vtable,
// breadcrumb max_items, commandpalette clear, menu management,
// and codeeditor data fields.
#include "vg_ide_widgets.h"
#include "vg_event.h"
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
        printf("  %-55s", #name "...");                                                            \
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
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

//=============================================================================
// Group E1 — vg_slider (vtable implementation)
//=============================================================================

TEST(slider_create_vtable_set) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    ASSERT_NEQ(s->base.vtable, NULL); // vtable must be assigned by create
    vg_widget_destroy(&s->base);
}

TEST(slider_default_orientation) {
    vg_slider_t *h = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(h);
    ASSERT_EQ(h->orientation, VG_SLIDER_HORIZONTAL);
    vg_widget_destroy(&h->base);

    vg_slider_t *v = vg_slider_create(NULL, VG_SLIDER_VERTICAL);
    ASSERT_NOT_NULL(v);
    ASSERT_EQ(v->orientation, VG_SLIDER_VERTICAL);
    vg_widget_destroy(&v->base);
}

TEST(slider_set_get_value) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 100.0f);
    vg_slider_set_value(s, 50.0f);
    ASSERT_EQ(vg_slider_get_value(s), 50.0f);
    vg_widget_destroy(&s->base);
}

TEST(slider_clamp_below_min) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 10.0f, 90.0f);
    vg_slider_set_value(s, -5.0f);
    ASSERT_EQ(vg_slider_get_value(s), 10.0f);
    vg_widget_destroy(&s->base);
}

TEST(slider_clamp_above_max) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 10.0f, 90.0f);
    vg_slider_set_value(s, 200.0f);
    ASSERT_EQ(vg_slider_get_value(s), 90.0f);
    vg_widget_destroy(&s->base);
}

TEST(slider_step_snapping) {
    vg_slider_t *s = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    ASSERT_NOT_NULL(s);
    vg_slider_set_range(s, 0.0f, 10.0f);
    vg_slider_set_step(s, 1.0f);
    vg_slider_set_value(s, 3.7f);
    float v = vg_slider_get_value(s);
    // Value must snap to a multiple of step
    ASSERT(v == 3.0f || v == 4.0f);
    vg_widget_destroy(&s->base);
}

//=============================================================================
// Group E2 — vg_progressbar (vtable implementation)
//=============================================================================

TEST(progressbar_create_vtable_set) {
    vg_progressbar_t *pb = vg_progressbar_create(NULL);
    ASSERT_NOT_NULL(pb);
    ASSERT_NEQ(pb->base.vtable, NULL);
    vg_widget_destroy(&pb->base);
}

TEST(progressbar_default_zero) {
    vg_progressbar_t *pb = vg_progressbar_create(NULL);
    ASSERT_NOT_NULL(pb);
    ASSERT_EQ(vg_progressbar_get_value(pb), 0.0f);
    vg_widget_destroy(&pb->base);
}

TEST(progressbar_set_value) {
    vg_progressbar_t *pb = vg_progressbar_create(NULL);
    ASSERT_NOT_NULL(pb);
    vg_progressbar_set_value(pb, 0.75f);
    ASSERT_EQ(vg_progressbar_get_value(pb), 0.75f);
    vg_widget_destroy(&pb->base);
}

TEST(progressbar_clamp_below_zero) {
    vg_progressbar_t *pb = vg_progressbar_create(NULL);
    ASSERT_NOT_NULL(pb);
    vg_progressbar_set_value(pb, -0.5f);
    ASSERT_EQ(vg_progressbar_get_value(pb), 0.0f);
    vg_widget_destroy(&pb->base);
}

TEST(progressbar_clamp_above_one) {
    vg_progressbar_t *pb = vg_progressbar_create(NULL);
    ASSERT_NOT_NULL(pb);
    vg_progressbar_set_value(pb, 1.5f);
    ASSERT_EQ(vg_progressbar_get_value(pb), 1.0f);
    vg_widget_destroy(&pb->base);
}

TEST(progressbar_style_change) {
    vg_progressbar_t *pb = vg_progressbar_create(NULL);
    ASSERT_NOT_NULL(pb);
    vg_progressbar_set_style(pb, VG_PROGRESS_INDETERMINATE);
    ASSERT_EQ(pb->style, VG_PROGRESS_INDETERMINATE);
    vg_progressbar_set_style(pb, VG_PROGRESS_BAR);
    ASSERT_EQ(pb->style, VG_PROGRESS_BAR);
    vg_widget_destroy(&pb->base);
}

TEST(progressbar_tick_advances_indeterminate_phase) {
    vg_progressbar_t *pb = vg_progressbar_create(NULL);
    ASSERT_NOT_NULL(pb);
    vg_progressbar_set_style(pb, VG_PROGRESS_INDETERMINATE);
    vg_progressbar_tick(pb, 0.5f);
    ASSERT(pb->animation_phase > 0.34f && pb->animation_phase < 0.36f);
    ASSERT(pb->base.needs_paint);
    vg_widget_destroy(&pb->base);
}

//=============================================================================
// Group E2b — Spinner
//=============================================================================

TEST(spinner_create_vtable_set) {
    vg_spinner_t *spinner = vg_spinner_create(NULL);
    ASSERT_NOT_NULL(spinner);
    ASSERT_NEQ(spinner->base.vtable, NULL);
    vg_widget_destroy(&spinner->base);
}

TEST(spinner_arrow_keys_adjust_value) {
    vg_spinner_t *spinner = vg_spinner_create(NULL);
    ASSERT_NOT_NULL(spinner);
    vg_spinner_set_range(spinner, 0.0, 10.0);
    vg_spinner_set_step(spinner, 2.0);

    vg_event_t up = {0};
    up.type = VG_EVENT_KEY_DOWN;
    up.key.key = VG_KEY_UP;
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &up));
    ASSERT_EQ(vg_spinner_get_value(spinner), 2.0);

    vg_event_t down = up;
    down.key.key = VG_KEY_DOWN;
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &down));
    ASSERT_EQ(vg_spinner_get_value(spinner), 0.0);

    vg_widget_destroy(&spinner->base);
}

TEST(spinner_mouse_buttons_adjust_value) {
    vg_spinner_t *spinner = vg_spinner_create(NULL);
    ASSERT_NOT_NULL(spinner);
    spinner->base.width = 80.0f;
    spinner->base.height = 28.0f;
    vg_spinner_set_range(spinner, 0.0, 10.0);
    vg_spinner_set_step(spinner, 1.0);

    vg_event_t down = {0};
    down.type = VG_EVENT_MOUSE_DOWN;
    down.mouse.x = 70.0f;
    down.mouse.y = 5.0f;
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &down));
    ASSERT_EQ(vg_spinner_get_value(spinner), 1.0);

    vg_event_t up = down;
    up.type = VG_EVENT_MOUSE_UP;
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &up));

    down.mouse.y = 24.0f;
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &down));
    ASSERT_EQ(vg_spinner_get_value(spinner), 0.0);

    vg_widget_destroy(&spinner->base);
}

TEST(spinner_text_entry_commits_value) {
    vg_spinner_t *spinner = vg_spinner_create(NULL);
    ASSERT_NOT_NULL(spinner);
    vg_spinner_set_range(spinner, -100.0, 100.0);

    vg_event_t ch4 = {0};
    ch4.type = VG_EVENT_KEY_CHAR;
    ch4.key.codepoint = '4';
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &ch4));

    vg_event_t ch2 = ch4;
    ch2.key.codepoint = '2';
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &ch2));
    ASSERT(spinner->editing);
    ASSERT_STR_EQ(spinner->text_buffer, "42");

    vg_event_t enter = {0};
    enter.type = VG_EVENT_KEY_DOWN;
    enter.key.key = VG_KEY_ENTER;
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &enter));
    ASSERT_EQ(vg_spinner_get_value(spinner), 42.0);
    ASSERT(!spinner->editing);

    vg_widget_destroy(&spinner->base);
}

TEST(spinner_escape_cancels_pending_edit) {
    vg_spinner_t *spinner = vg_spinner_create(NULL);
    ASSERT_NOT_NULL(spinner);
    vg_spinner_set_value(spinner, 12.0);

    vg_event_t ch9 = {0};
    ch9.type = VG_EVENT_KEY_CHAR;
    ch9.key.codepoint = '9';
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &ch9));
    ASSERT(spinner->editing);
    ASSERT_STR_EQ(spinner->text_buffer, "9");

    vg_event_t esc = {0};
    esc.type = VG_EVENT_KEY_DOWN;
    esc.key.key = VG_KEY_ESCAPE;
    ASSERT(spinner->base.vtable->handle_event(&spinner->base, &esc));
    ASSERT_EQ(vg_spinner_get_value(spinner), 12.0);
    ASSERT_STR_EQ(spinner->text_buffer, "12");
    ASSERT(!spinner->editing);

    vg_widget_destroy(&spinner->base);
}

//=============================================================================
// Group E2c — Image
//=============================================================================

TEST(image_scale_none_preserves_original_size) {
    vgfx_window_params_t params = {
        .width = 8, .height = 8, .title = "image", .fps = 0, .resizable = 0};
    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vg_image_t *image = vg_image_create(NULL);
    ASSERT_NOT_NULL(image);

    const uint8_t pixel[4] = {255, 0, 0, 255};
    vg_image_set_pixels(image, pixel, 1, 1);
    vg_image_set_scale_mode(image, VG_IMAGE_SCALE_NONE);
    image->bg_color = 0x000000;
    vg_widget_arrange(&image->base, 0.0f, 0.0f, 3.0f, 3.0f);

    vgfx_cls(win, VGFX_BLACK);
    vg_widget_paint(&image->base, win);

    vgfx_color_t color = 0;
    ASSERT_EQ(vgfx_point(win, 0, 0, &color), 1);
    ASSERT_EQ(color, 0xFF0000);
    ASSERT_EQ(vgfx_point(win, 2, 2, &color), 1);
    ASSERT_EQ(color, 0x000000);

    vg_widget_destroy(&image->base);
    vgfx_destroy_window(win);
}

TEST(image_opacity_and_stretch_affect_framebuffer) {
    vgfx_window_params_t params = {
        .width = 8, .height = 8, .title = "image", .fps = 0, .resizable = 0};
    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vg_image_t *image = vg_image_create(NULL);
    ASSERT_NOT_NULL(image);

    const uint8_t pixel[4] = {255, 0, 0, 255};
    vg_image_set_pixels(image, pixel, 1, 1);
    vg_image_set_scale_mode(image, VG_IMAGE_SCALE_STRETCH);
    vg_image_set_opacity(image, 0.5f);
    vg_widget_arrange(&image->base, 0.0f, 0.0f, 2.0f, 2.0f);

    vgfx_cls(win, VGFX_BLACK);
    vg_widget_paint(&image->base, win);

    vgfx_color_t color = 0;
    ASSERT_EQ(vgfx_point(win, 1, 1, &color), 1);
    ASSERT(color == 0x800000 || color == 0x7F0000);

    vg_widget_destroy(&image->base);
    vgfx_destroy_window(win);
}

//=============================================================================
// Group E3 — vg_listbox (vtable implementation)
//=============================================================================

TEST(listbox_create_vtable_set) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    ASSERT_NEQ(lb->base.vtable, NULL);
    vg_widget_destroy(&lb->base);
}

TEST(listbox_add_items_count) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "Alpha", NULL);
    vg_listbox_add_item(lb, "Beta", NULL);
    vg_listbox_add_item(lb, "Gamma", NULL);
    ASSERT_EQ(lb->item_count, 3);
    vg_widget_destroy(&lb->base);
}

TEST(listbox_no_initial_selection) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "Item", NULL);
    ASSERT_NULL(vg_listbox_get_selected(lb));
    vg_widget_destroy(&lb->base);
}

TEST(listbox_select_item) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_item_t *item = vg_listbox_add_item(lb, "Item", NULL);
    ASSERT_NOT_NULL(item);
    vg_listbox_select(lb, item);
    ASSERT_EQ(vg_listbox_get_selected(lb), item);
    vg_widget_destroy(&lb->base);
}

TEST(listbox_remove_item) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "A", NULL);
    vg_listbox_item_t *b = vg_listbox_add_item(lb, "B", NULL);
    ASSERT_EQ(lb->item_count, 2);
    vg_listbox_remove_item(lb, b);
    ASSERT_EQ(lb->item_count, 1);
    vg_widget_destroy(&lb->base);
}

TEST(listbox_remove_clears_selection) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_item_t *item = vg_listbox_add_item(lb, "X", NULL);
    vg_listbox_select(lb, item);
    ASSERT_EQ(vg_listbox_get_selected(lb), item);
    vg_listbox_remove_item(lb, item);
    ASSERT_NULL(vg_listbox_get_selected(lb));
    vg_widget_destroy(&lb->base);
}

TEST(listbox_clear_empties_list) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);
    vg_listbox_add_item(lb, "A", NULL);
    vg_listbox_add_item(lb, "B", NULL);
    vg_listbox_add_item(lb, "C", NULL);
    ASSERT_EQ(lb->item_count, 3);
    vg_listbox_clear(lb);
    ASSERT_EQ(lb->item_count, 0);
    ASSERT_NULL(lb->first_item);
    ASSERT_NULL(lb->last_item);
    vg_widget_destroy(&lb->base);
}

TEST(listbox_mutations_invalidate_layout_and_paint) {
    vg_listbox_t *lb = vg_listbox_create(NULL);
    ASSERT_NOT_NULL(lb);

    lb->base.needs_layout = false;
    lb->base.needs_paint = false;
    vg_listbox_item_t *item = vg_listbox_add_item(lb, "Item", NULL);
    ASSERT_NOT_NULL(item);
    ASSERT(lb->base.needs_layout);
    ASSERT(lb->base.needs_paint);

    lb->base.needs_layout = false;
    lb->base.needs_paint = false;
    vg_listbox_select(lb, item);
    ASSERT(!lb->base.needs_layout);
    ASSERT(lb->base.needs_paint);

    lb->base.needs_layout = false;
    lb->base.needs_paint = false;
    vg_listbox_clear(lb);
    ASSERT(lb->base.needs_layout);
    ASSERT(lb->base.needs_paint);

    vg_widget_destroy(&lb->base);
}

//=============================================================================
// Group D-other — vg_breadcrumb max_items (new feature)
//=============================================================================

TEST(breadcrumb_push_pop_basic) {
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    ASSERT_NOT_NULL(bc);
    vg_breadcrumb_push(bc, "Root", NULL);
    vg_breadcrumb_push(bc, "Folder", NULL);
    ASSERT_EQ((int)bc->item_count, 2);
    vg_breadcrumb_pop(bc);
    ASSERT_EQ((int)bc->item_count, 1);
    vg_breadcrumb_destroy(bc);
}

TEST(breadcrumb_clear_resets) {
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    ASSERT_NOT_NULL(bc);
    vg_breadcrumb_push(bc, "A", NULL);
    vg_breadcrumb_push(bc, "B", NULL);
    vg_breadcrumb_push(bc, "C", NULL);
    vg_breadcrumb_clear(bc);
    ASSERT_EQ((int)bc->item_count, 0);
    vg_breadcrumb_destroy(bc);
}

TEST(breadcrumb_max_items_sliding_window) {
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    ASSERT_NOT_NULL(bc);
    vg_breadcrumb_set_max_items(bc, 3);
    vg_breadcrumb_push(bc, "A", NULL);
    vg_breadcrumb_push(bc, "B", NULL);
    vg_breadcrumb_push(bc, "C", NULL);
    ASSERT_EQ((int)bc->item_count, 3);
    // Push 4th — oldest (A) must be evicted
    vg_breadcrumb_push(bc, "D", NULL);
    ASSERT_EQ((int)bc->item_count, 3);
    ASSERT_STR_EQ(bc->items[0].label, "B");
    ASSERT_STR_EQ(bc->items[2].label, "D");
    vg_breadcrumb_destroy(bc);
}

TEST(breadcrumb_set_max_trims_existing) {
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    ASSERT_NOT_NULL(bc);
    vg_breadcrumb_push(bc, "A", NULL);
    vg_breadcrumb_push(bc, "B", NULL);
    vg_breadcrumb_push(bc, "C", NULL);
    vg_breadcrumb_push(bc, "D", NULL);
    ASSERT_EQ((int)bc->item_count, 4);
    // Restrict to 2 — oldest two (A, B) get trimmed
    vg_breadcrumb_set_max_items(bc, 2);
    ASSERT_EQ((int)bc->item_count, 2);
    ASSERT_STR_EQ(bc->items[0].label, "C");
    ASSERT_STR_EQ(bc->items[1].label, "D");
    vg_breadcrumb_destroy(bc);
}

TEST(breadcrumb_separator_change) {
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    ASSERT_NOT_NULL(bc);
    ASSERT_NOT_NULL(bc->separator); // Default is ">"
    vg_breadcrumb_set_separator(bc, "/");
    ASSERT_STR_EQ(bc->separator, "/");
    vg_breadcrumb_set_separator(bc, NULL);
    ASSERT_NULL(bc->separator);
    vg_breadcrumb_destroy(bc);
}

TEST(breadcrumb_user_data_not_owned_by_default) {
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    ASSERT_NOT_NULL(bc);
    char *payload = strdup("payload");
    ASSERT_NOT_NULL(payload);
    vg_breadcrumb_push(bc, "A", payload);
    ASSERT_EQ((int)bc->item_count, 1);
    ASSERT(!bc->items[0].owns_user_data);
    vg_breadcrumb_clear(bc);
    free(payload);
    vg_breadcrumb_destroy(bc);
}

//=============================================================================
// Group D-other — vg_commandpalette clear (new feature)
//=============================================================================

TEST(commandpalette_create_basic) {
    vg_commandpalette_t *p = vg_commandpalette_create();
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((int)p->command_count, 0);
    ASSERT(!p->is_visible);
    vg_commandpalette_destroy(p);
}

TEST(commandpalette_add_and_find) {
    vg_commandpalette_t *p = vg_commandpalette_create();
    ASSERT_NOT_NULL(p);
    vg_command_t *cmd =
        vg_commandpalette_add_command(p, "file.open", "Open File", "Ctrl+O", NULL, NULL);
    ASSERT_NOT_NULL(cmd);
    ASSERT_EQ((int)p->command_count, 1);
    vg_command_t *found = vg_commandpalette_get_command(p, "file.open");
    ASSERT_EQ(found, cmd);
    vg_commandpalette_destroy(p);
}

TEST(commandpalette_remove_command) {
    vg_commandpalette_t *p = vg_commandpalette_create();
    vg_commandpalette_add_command(p, "a", "A", NULL, NULL, NULL);
    vg_commandpalette_add_command(p, "b", "B", NULL, NULL, NULL);
    ASSERT_EQ((int)p->command_count, 2);
    vg_commandpalette_remove_command(p, "a");
    ASSERT_EQ((int)p->command_count, 1);
    ASSERT_NULL(vg_commandpalette_get_command(p, "a"));
    ASSERT_NOT_NULL(vg_commandpalette_get_command(p, "b"));
    vg_commandpalette_destroy(p);
}

TEST(commandpalette_clear_all) {
    vg_commandpalette_t *p = vg_commandpalette_create();
    vg_commandpalette_add_command(p, "x", "X", NULL, NULL, NULL);
    vg_commandpalette_add_command(p, "y", "Y", NULL, NULL, NULL);
    vg_commandpalette_add_command(p, "z", "Z", NULL, NULL, NULL);
    ASSERT_EQ((int)p->command_count, 3);
    vg_commandpalette_clear(p);
    ASSERT_EQ((int)p->command_count, 0);
    ASSERT_EQ((int)p->filtered_count, 0);
    ASSERT_EQ(p->selected_index, -1);
    vg_commandpalette_destroy(p);
}

TEST(commandpalette_show_hide_toggle) {
    vg_commandpalette_t *p = vg_commandpalette_create();
    ASSERT(!p->is_visible);
    vg_commandpalette_show(p);
    ASSERT(p->is_visible);
    vg_commandpalette_hide(p);
    ASSERT(!p->is_visible);
    vg_commandpalette_toggle(p);
    ASSERT(p->is_visible);
    vg_commandpalette_toggle(p);
    ASSERT(!p->is_visible);
    vg_commandpalette_destroy(p);
}

//=============================================================================
// Group D-menu — menu management (new functions)
//=============================================================================

TEST(menu_remove_item_updates_count) {
    vg_menubar_t *bar = vg_menubar_create(NULL);
    ASSERT_NOT_NULL(bar);
    vg_menu_t *menu = vg_menubar_add_menu(bar, "File");
    ASSERT_NOT_NULL(menu);
    vg_menu_item_t *item1 = vg_menu_add_item(menu, "Open", "Ctrl+O", NULL, NULL);
    vg_menu_item_t *item2 = vg_menu_add_item(menu, "Save", "Ctrl+S", NULL, NULL);
    ASSERT_NOT_NULL(item1);
    ASSERT_NOT_NULL(item2);
    ASSERT_EQ(menu->item_count, 2);
    vg_menu_remove_item(menu, item1);
    ASSERT_EQ(menu->item_count, 1);
    ASSERT_EQ(menu->first_item, item2); // item2 is now first
    vg_widget_destroy(&bar->base);
}

TEST(menu_clear_empties_list) {
    vg_menubar_t *bar = vg_menubar_create(NULL);
    vg_menu_t *menu = vg_menubar_add_menu(bar, "Edit");
    vg_menu_add_item(menu, "Cut", NULL, NULL, NULL);
    vg_menu_add_item(menu, "Copy", NULL, NULL, NULL);
    vg_menu_add_item(menu, "Paste", NULL, NULL, NULL);
    ASSERT_EQ(menu->item_count, 3);
    vg_menu_clear(menu);
    ASSERT_EQ(menu->item_count, 0);
    ASSERT_NULL(menu->first_item);
    ASSERT_NULL(menu->last_item);
    vg_widget_destroy(&bar->base);
}

TEST(menubar_remove_menu_updates_count) {
    vg_menubar_t *bar = vg_menubar_create(NULL);
    vg_menu_t *file = vg_menubar_add_menu(bar, "File");
    vg_menubar_add_menu(bar, "Edit");
    vg_menu_t *help = vg_menubar_add_menu(bar, "Help");
    ASSERT_EQ(bar->menu_count, 3);
    vg_menubar_remove_menu(bar, file);
    ASSERT_EQ(bar->menu_count, 2);
    vg_menubar_remove_menu(bar, help);
    ASSERT_EQ(bar->menu_count, 1);
    vg_widget_destroy(&bar->base);
}

//=============================================================================
// Group D-editor — vg_codeeditor new dynamic array fields
//=============================================================================

TEST(codeeditor_highlight_spans_init_zero) {
    vg_codeeditor_t *ed = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(ed);
    ASSERT_EQ(ed->highlight_span_count, 0);
    ASSERT_EQ(ed->highlight_span_cap, 0);
    ASSERT_NULL(ed->highlight_spans);
    vg_widget_destroy(&ed->base);
}

TEST(codeeditor_gutter_icons_init_zero) {
    vg_codeeditor_t *ed = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(ed);
    ASSERT_EQ(ed->gutter_icon_count, 0);
    ASSERT_EQ(ed->gutter_icon_cap, 0);
    ASSERT_NULL(ed->gutter_icons);
    vg_widget_destroy(&ed->base);
}

TEST(codeeditor_fold_regions_init_zero) {
    vg_codeeditor_t *ed = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(ed);
    ASSERT_EQ(ed->fold_region_count, 0);
    ASSERT_EQ(ed->fold_region_cap, 0);
    ASSERT_NULL(ed->fold_regions);
    vg_widget_destroy(&ed->base);
}

TEST(codeeditor_extra_cursors_init_zero) {
    vg_codeeditor_t *ed = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(ed);
    ASSERT_EQ(ed->extra_cursor_count, 0);
    ASSERT_EQ(ed->extra_cursor_cap, 0);
    ASSERT_NULL(ed->extra_cursors);
    vg_widget_destroy(&ed->base);
}

TEST(codeeditor_tick_toggles_cursor_visibility) {
    vg_codeeditor_t *ed = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(ed);
    ed->base.state |= VG_STATE_FOCUSED;
    ed->cursor_visible = true;
    vg_codeeditor_tick(ed, 0.6f);
    ASSERT(ed->cursor_visible == false);
    ASSERT(ed->base.needs_paint);
    vg_widget_destroy(&ed->base);
}

TEST(codeeditor_mouse_wheel_scrolls_one_line_per_notch) {
    vg_codeeditor_t *ed = vg_codeeditor_create(NULL);
    ASSERT_NOT_NULL(ed);
    vg_codeeditor_set_text(ed, "one\ntwo\nthree\nfour\nfive\nsix\n");
    ed->base.width = 320.0f;
    ed->base.height = ed->line_height * 2.0f;
    ed->base.measured_width = ed->base.width;
    ed->base.measured_height = ed->base.height;

    vg_event_t wheel = {0};
    wheel.type = VG_EVENT_MOUSE_WHEEL;
    wheel.wheel.delta_y = -1.0f;
    ASSERT(ed->base.vtable->handle_event(&ed->base, &wheel));
    ASSERT(ed->scroll_y == ed->line_height);

    vg_widget_destroy(&ed->base);
}

//=============================================================================
// Entry Point
//=============================================================================

int main(void) {
    printf("=== VG Widget New Features Tests ===\n\n");

    printf("Group E1 — Slider vtable:\n");
    RUN(slider_create_vtable_set);
    RUN(slider_default_orientation);
    RUN(slider_set_get_value);
    RUN(slider_clamp_below_min);
    RUN(slider_clamp_above_max);
    RUN(slider_step_snapping);

    printf("\nGroup E2 — ProgressBar vtable:\n");
    RUN(progressbar_create_vtable_set);
    RUN(progressbar_default_zero);
    RUN(progressbar_set_value);
    RUN(progressbar_clamp_below_zero);
    RUN(progressbar_clamp_above_one);
    RUN(progressbar_style_change);
    RUN(progressbar_tick_advances_indeterminate_phase);

    printf("\nGroup E2b — Spinner:\n");
    RUN(spinner_create_vtable_set);
    RUN(spinner_arrow_keys_adjust_value);
    RUN(spinner_mouse_buttons_adjust_value);
    RUN(spinner_text_entry_commits_value);
    RUN(spinner_escape_cancels_pending_edit);

    printf("\nGroup E2c — Image:\n");
    RUN(image_scale_none_preserves_original_size);
    RUN(image_opacity_and_stretch_affect_framebuffer);

    printf("\nGroup E3 — ListBox vtable:\n");
    RUN(listbox_create_vtable_set);
    RUN(listbox_add_items_count);
    RUN(listbox_no_initial_selection);
    RUN(listbox_select_item);
    RUN(listbox_remove_item);
    RUN(listbox_remove_clears_selection);
    RUN(listbox_clear_empties_list);
    RUN(listbox_mutations_invalidate_layout_and_paint);

    printf("\nGroup D-other — Breadcrumb max_items:\n");
    RUN(breadcrumb_push_pop_basic);
    RUN(breadcrumb_clear_resets);
    RUN(breadcrumb_max_items_sliding_window);
    RUN(breadcrumb_set_max_trims_existing);
    RUN(breadcrumb_separator_change);

    printf("\nGroup D-other — CommandPalette clear:\n");
    RUN(commandpalette_create_basic);
    RUN(commandpalette_add_and_find);
    RUN(commandpalette_remove_command);
    RUN(commandpalette_clear_all);
    RUN(commandpalette_show_hide_toggle);

    printf("\nGroup D-menu — Menu management:\n");
    RUN(menu_remove_item_updates_count);
    RUN(menu_clear_empties_list);
    RUN(menubar_remove_menu_updates_count);

    printf("\nGroup D-editor — CodeEditor new fields:\n");
    RUN(codeeditor_highlight_spans_init_zero);
    RUN(codeeditor_gutter_icons_init_zero);
    RUN(codeeditor_fold_regions_init_zero);
    RUN(codeeditor_extra_cursors_init_zero);
    RUN(codeeditor_tick_toggles_cursor_visibility);
    RUN(codeeditor_mouse_wheel_scrolls_one_line_per_notch);

    printf("\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
