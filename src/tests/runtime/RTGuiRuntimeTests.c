//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGuiRuntimeTests.c
// Purpose: Regression tests for GUI runtime app-scoped state.
//
//===----------------------------------------------------------------------===//

#include "../../runtime/graphics/rt_gui_internal.h"
#include "rt_gui.h"
#include "rt_pixels.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

extern void rt_gui_set_clicked_statusbar_item(void *item);

typedef struct {
    uint64_t magic;
    vg_findreplacebar_t *bar;
    void *bound_editor;
    char *find_text;
    char *replace_text;
    int64_t case_sensitive;
    int64_t whole_word;
    int64_t regex;
    int64_t replace_mode;
} rt_findbar_data_view_t;

typedef struct {
    uint64_t magic;
    vg_breadcrumb_t *breadcrumb;
    int64_t clicked_index;
    rt_gui_string_data_t *clicked_data;
    int64_t was_clicked;
} rt_breadcrumb_data_view_t;

typedef struct {
    uint64_t magic;
    rt_gui_app_t *owner_app;
    vg_filedialog_t *dialog;
    char **selected_paths;
    size_t selected_count;
    int64_t result;
} rt_filedialog_data_view_t;

typedef struct {
    uint64_t magic;
    vg_dialog_t *dialog;
    int64_t result;
    int64_t default_button;
    rt_gui_app_t *owner_app;
    vg_dialog_button_def_t *custom_buttons;
    int64_t *custom_button_ids;
    size_t custom_button_count;
    size_t custom_button_cap;
} rt_messagebox_data_view_t;

typedef struct {
    uint64_t magic;
    rt_gui_app_t *app;
    vg_commandpalette_t *palette;
    char *selected_command;
    int64_t was_selected;
} rt_commandpalette_data_view_t;

static char *test_strdup_local(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy)
        memcpy(copy, s, len);
    return copy;
}

void vm_trap(const char *msg) {
    (void)msg;
    assert(0 && "unexpected vm_trap");
}

static void reset_fake_app(rt_gui_app_t *app) {
    memset(app, 0, sizeof(*app));
    app->magic = RT_GUI_APP_MAGIC;
    app->shortcuts_global_enabled = 1;
    app->theme_kind = RT_GUI_THEME_DARK;
}

static void cleanup_fake_app(rt_gui_app_t *app) {
    if (!app)
        return;
    rt_gui_activate_app(NULL);
    if (app->root) {
        vg_widget_destroy(app->root);
        app->root = NULL;
    }
    if (app->theme) {
        vg_theme_destroy(app->theme);
        app->theme = NULL;
    }
    free(app->retired_fonts);
    app->retired_fonts = NULL;
    app->retired_font_count = 0;
    app->retired_font_cap = 0;
}

static void test_shortcuts_are_app_scoped(void) {
    rt_gui_app_t app_a;
    rt_gui_app_t app_b;
    reset_fake_app(&app_a);
    reset_fake_app(&app_b);

    s_current_app = &app_a;
    rt_shortcuts_register(rt_const_cstr("save"), rt_const_cstr("Ctrl+S"), rt_const_cstr(""));
    s_current_app = &app_b;
    rt_shortcuts_register(rt_const_cstr("help"), rt_const_cstr("F5"), rt_const_cstr(""));

    rt_shortcuts_clear_triggered(&app_a);
    rt_shortcuts_clear_triggered(&app_b);

    assert(rt_shortcuts_check_key(&app_a, 'S', VGFX_MOD_CTRL) == 1);

    s_current_app = &app_a;
    assert(rt_shortcuts_was_triggered(rt_const_cstr("save")) == 1);
    assert(rt_shortcuts_was_triggered(rt_const_cstr("help")) == 0);

    s_current_app = &app_b;
    assert(rt_shortcuts_check_key(&app_b, VG_KEY_F5, 0) == 1);
    assert(rt_shortcuts_was_triggered(rt_const_cstr("help")) == 1);
    assert(rt_shortcuts_was_triggered(rt_const_cstr("save")) == 0);

    s_current_app = &app_a;
    rt_shortcuts_clear();
    s_current_app = &app_b;
    rt_shortcuts_clear();

    printf("test_shortcuts_are_app_scoped: PASSED\n");
}

static void test_file_drop_is_app_scoped(void) {
    rt_gui_app_t app_a;
    rt_gui_app_t app_b;
    reset_fake_app(&app_a);
    reset_fake_app(&app_b);

    rt_gui_file_drop_add(&app_a, "/tmp/a.txt");
    rt_gui_file_drop_add(&app_b, "/tmp/b.txt");

    rt_gui_activate_app(&app_a);
    assert(rt_app_was_file_dropped(&app_a) == 1);
    assert(rt_app_get_dropped_file_count(&app_a) == 1);
    assert(strcmp(rt_string_cstr(rt_app_get_dropped_file(&app_a, 0)), "/tmp/a.txt") == 0);

    rt_gui_activate_app(&app_b);
    assert(rt_app_was_file_dropped(&app_b) == 1);
    assert(rt_app_get_dropped_file_count(&app_b) == 1);
    assert(strcmp(rt_string_cstr(rt_app_get_dropped_file(&app_b, 0)), "/tmp/b.txt") == 0);

    rt_gui_activate_app(&app_a);
    assert(rt_app_was_file_dropped(&app_a) == 0);
    rt_gui_activate_app(&app_b);
    assert(rt_app_was_file_dropped(&app_b) == 0);

    rt_gui_features_cleanup(&app_a);
    rt_gui_features_cleanup(&app_b);
    rt_gui_activate_app(NULL);

    printf("test_file_drop_is_app_scoped: PASSED\n");
}

static void test_statusbar_click_is_edge_triggered(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_statusbar_t *statusbar = vg_statusbar_create(app.root);
    assert(statusbar);
    vg_statusbar_item_t *item =
        vg_statusbar_add_text(statusbar, VG_STATUSBAR_ZONE_LEFT, "ready");
    assert(item);

    rt_gui_set_clicked_statusbar_item(item);
    assert(rt_statusbaritem_was_clicked(item) == 1);
    assert(rt_statusbaritem_was_clicked(item) == 0);

    cleanup_fake_app(&app);
    printf("test_statusbar_click_is_edge_triggered: PASSED\n");
}

static void test_statusbar_runtime_button_wires_click_polling(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_statusbar_t *statusbar = (vg_statusbar_t *)rt_statusbar_new(app.root);
    assert(statusbar);
    vg_statusbar_item_t *button =
        (vg_statusbar_item_t *)rt_statusbar_add_button(statusbar, rt_const_cstr("Build"), 0);
    assert(button);
    assert(button->on_click != NULL);

    button->on_click(button, button->user_data);
    assert(rt_statusbaritem_was_clicked(button) == 1);
    assert(rt_statusbaritem_was_clicked(button) == 0);

    cleanup_fake_app(&app);
    printf("test_statusbar_runtime_button_wires_click_polling: PASSED\n");
}

static void test_default_font_is_applied_to_text_widgets(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.default_font = (vg_font_t *)0x1;
    app.default_font_size = 17.0f;
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_label_t *label = (vg_label_t *)rt_label_new(app.root, rt_const_cstr("label"));
    vg_button_t *button = (vg_button_t *)rt_button_new(app.root, rt_const_cstr("button"));
    vg_textinput_t *input = (vg_textinput_t *)rt_textinput_new(app.root);
    vg_checkbox_t *checkbox = (vg_checkbox_t *)rt_checkbox_new(app.root, rt_const_cstr("check"));
    vg_dropdown_t *dropdown = (vg_dropdown_t *)rt_dropdown_new(app.root);
    vg_listbox_t *listbox = (vg_listbox_t *)rt_listbox_new(app.root);
    void *group = rt_radiogroup_new();
    vg_radiobutton_t *radio =
        (vg_radiobutton_t *)rt_radiobutton_new(app.root, rt_const_cstr("radio"), group);

    assert(label && label->font == app.default_font && label->font_size == app.default_font_size);
    assert(button && button->font == app.default_font &&
           button->font_size == app.default_font_size);
    assert(input && input->font == app.default_font && input->font_size == app.default_font_size);
    assert(checkbox && checkbox->font == app.default_font &&
           checkbox->font_size == app.default_font_size);
    assert(dropdown && dropdown->font == app.default_font &&
           dropdown->font_size == app.default_font_size);
    assert(listbox && listbox->font == app.default_font &&
           listbox->font_size == app.default_font_size);
    assert(radio && radio->font == app.default_font && radio->font_size == app.default_font_size);

    rt_radiogroup_destroy(group);
    cleanup_fake_app(&app);
    printf("test_default_font_is_applied_to_text_widgets: PASSED\n");
}

static void test_default_font_is_applied_to_complex_text_widgets(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.default_font = (vg_font_t *)0x1;
    app.default_font_size = 17.0f;
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_treeview_t *tree = (vg_treeview_t *)rt_treeview_new(app.root);
    vg_tabbar_t *tabbar = (vg_tabbar_t *)rt_tabbar_new(app.root);
    vg_codeeditor_t *editor = (vg_codeeditor_t *)rt_codeeditor_new(app.root);
    vg_slider_t *slider = (vg_slider_t *)rt_slider_new(app.root, 1);
    vg_progressbar_t *progress = (vg_progressbar_t *)rt_progressbar_new(app.root);
    vg_spinner_t *spinner = (vg_spinner_t *)rt_spinner_new(app.root);

    assert(tree && tree->font == app.default_font && tree->font_size == app.default_font_size);
    assert(tabbar && tabbar->font == app.default_font &&
           tabbar->font_size == app.default_font_size);
    assert(editor && editor->font == app.default_font &&
           editor->font_size == app.default_font_size);
    assert(slider && slider->font == app.default_font &&
           slider->font_size == app.default_font_size);
    assert(progress && progress->font == app.default_font &&
           progress->font_size == app.default_font_size);
    assert(spinner && spinner->font == app.default_font &&
           spinner->font_size == app.default_font_size);

    cleanup_fake_app(&app);
    printf("test_default_font_is_applied_to_complex_text_widgets: PASSED\n");
}

static void test_widget_set_font_rejects_stale_font_handles(void) {
    vg_font_t *sentinel = (vg_font_t *)(uintptr_t)0x1000;
    void *stale = (void *)(uintptr_t)0x12345678;

    vg_label_t *label = vg_label_create(NULL, "label");
    vg_button_t *button = vg_button_create(NULL, "button");
    vg_textinput_t *input = vg_textinput_create(NULL);
    vg_treeview_t *tree = vg_treeview_create(NULL);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    vg_listbox_t *listbox = vg_listbox_create(NULL);
    assert(label && button && input && tree && editor && listbox);

    label->font = sentinel;
    label->font_size = 11.0f;
    button->font = sentinel;
    button->font_size = 12.0f;
    input->font = sentinel;
    input->font_size = 13.0f;
    tree->font = sentinel;
    tree->font_size = 14.0f;
    editor->font = sentinel;
    editor->font_size = 15.0f;
    listbox->font = sentinel;
    listbox->font_size = 16.0f;

    rt_label_set_font(label, stale, 21.0);
    rt_button_set_font(button, stale, 22.0);
    rt_textinput_set_font(input, stale, 23.0);
    rt_treeview_set_font(tree, stale, 24.0);
    rt_codeeditor_set_font(editor, stale, 25.0);
    rt_listbox_set_font(listbox, stale, 26.0);

    assert(label->font == sentinel && label->font_size == 11.0f);
    assert(button->font == sentinel && button->font_size == 12.0f);
    assert(input->font == sentinel && input->font_size == 13.0f);
    assert(tree->font == sentinel && tree->font_size == 14.0f);
    assert(editor->font == sentinel && editor->font_size == 15.0f);
    assert(listbox->font == sentinel && listbox->font_size == 16.0f);

    vg_widget_destroy(&listbox->base);
    vg_widget_destroy(&editor->base);
    vg_widget_destroy(&tree->base);
    vg_widget_destroy(&input->base);
    vg_widget_destroy(&button->base);
    vg_widget_destroy(&label->base);
    printf("test_widget_set_font_rejects_stale_font_handles: PASSED\n");
}

static void test_dropdown_placeholder_is_copied(void) {
    vg_dropdown_t *dropdown = vg_dropdown_create(NULL);
    char placeholder[] = "Choose item";

    vg_dropdown_set_placeholder(dropdown, placeholder);
    placeholder[0] = 'X';

    assert(dropdown->placeholder);
    assert(strcmp(dropdown->placeholder, "Choose item") == 0);

    vg_widget_destroy(&dropdown->base);
    printf("test_dropdown_placeholder_is_copied: PASSED\n");
}

static void test_dialog_content_is_parented(void) {
    vg_dialog_t *dialog = vg_dialog_create("Dialog");
    vg_textinput_t *input = vg_textinput_create(NULL);

    vg_dialog_set_content(dialog, &input->base);

    assert(dialog->content == &input->base);
    assert(input->base.parent == &dialog->base);
    assert(dialog->base.first_child == &input->base);

    vg_widget_destroy(&dialog->base);
    printf("test_dialog_content_is_parented: PASSED\n");
}

static void test_notification_cleanup_runs_for_manual_dismiss(void) {
    vg_notification_manager_t *mgr = vg_notification_manager_create();
    uint32_t id_a = vg_notification_show(mgr, VG_NOTIFICATION_INFO, "A", "first", 0);
    uint32_t id_b = vg_notification_show(mgr, VG_NOTIFICATION_INFO, "B", "second", 0);

    assert(mgr->notification_count == 2);
    vg_notification_dismiss(mgr, id_a);
    vg_notification_manager_update(mgr, 1234);

    assert(mgr->notification_count == 1);
    assert(mgr->notifications[0] && mgr->notifications[0]->id == id_b);

    vg_notification_manager_destroy(mgr);
    printf("test_notification_cleanup_runs_for_manual_dismiss: PASSED\n");
}

static void test_notification_ids_skip_zero_on_wrap(void) {
    vg_notification_manager_t *mgr = vg_notification_manager_create();
    assert(mgr);
    mgr->next_id = UINT32_MAX;

    uint32_t max_id = vg_notification_show(mgr, VG_NOTIFICATION_INFO, "A", "first", 0);
    uint32_t wrapped_id = vg_notification_show(mgr, VG_NOTIFICATION_INFO, "B", "second", 0);

    assert(max_id == UINT32_MAX);
    assert(wrapped_id == 1);
    assert(mgr->notification_count == 2);
    assert(mgr->notifications[0]->id == UINT32_MAX);
    assert(mgr->notifications[1]->id == 1);

    vg_notification_manager_destroy(mgr);
    printf("test_notification_ids_skip_zero_on_wrap: PASSED\n");
}

static void test_command_palette_placeholder_and_utf8_input(void) {
    vg_commandpalette_t *palette = vg_commandpalette_create();
    vg_commandpalette_set_placeholder(palette, "Run action");
    assert(palette->placeholder_text);
    assert(strcmp(palette->placeholder_text, "Run action") == 0);

    vg_commandpalette_show(palette);
    vg_event_t text_event = vg_event_key(VG_EVENT_KEY_CHAR, VG_KEY_UNKNOWN, 0x00E9, 0);
    assert(palette->base.vtable->handle_event(&palette->base, &text_event));
    assert(palette->current_query);
    assert(strcmp(palette->current_query, "\xC3\xA9") == 0);

    vg_event_t backspace = vg_event_key(VG_EVENT_KEY_DOWN, VG_KEY_BACKSPACE, 0, 0);
    assert(palette->base.vtable->handle_event(&palette->base, &backspace));
    assert(palette->current_query == NULL);

    vg_commandpalette_destroy(palette);
    printf("test_command_palette_placeholder_and_utf8_input: PASSED\n");
}

static void test_platform_text_events_translate_to_gui_text(void) {
    vgfx_event_t platform_event = {0};
    platform_event.type = VGFX_EVENT_KEY_DOWN;
    platform_event.data.key.key = VGFX_KEY_A;
    platform_event.data.key.modifiers = VGFX_MOD_SHIFT | VGFX_MOD_CTRL;

    vg_event_t gui_event = vg_event_from_platform(&platform_event);
    assert(gui_event.type == VG_EVENT_KEY_DOWN);
    assert(gui_event.key.key == VG_KEY_A);
    assert(gui_event.modifiers == (VG_MOD_SHIFT | VG_MOD_CTRL));

    platform_event.type = VGFX_EVENT_TEXT_INPUT;
    platform_event.data.text.codepoint = 0x00E9;
    platform_event.data.text.modifiers = VGFX_MOD_ALT;

    gui_event = vg_event_from_platform(&platform_event);
    assert(gui_event.type == VG_EVENT_KEY_CHAR);
    assert(gui_event.key.codepoint == 0x00E9);
    assert(gui_event.modifiers == VG_MOD_ALT);

    printf("test_platform_text_events_translate_to_gui_text: PASSED\n");
}

static void test_platform_scroll_events_keep_screen_coordinates_separate(void) {
    vgfx_event_t platform_event = {0};
    platform_event.type = VGFX_EVENT_SCROLL;
    platform_event.data.scroll.delta_x = 1.25f;
    platform_event.data.scroll.delta_y = -2.5f;
    platform_event.data.scroll.x = 42;
    platform_event.data.scroll.y = 84;

    vg_event_t gui_event = vg_event_from_platform(&platform_event);
    assert(gui_event.type == VG_EVENT_MOUSE_WHEEL);
    assert(gui_event.wheel.delta_x == 1.25f);
    assert(gui_event.wheel.delta_y == -2.5f);
    assert(gui_event.wheel.screen_x == 42.0f);
    assert(gui_event.wheel.screen_y == 84.0f);

    printf("test_platform_scroll_events_keep_screen_coordinates_separate: PASSED\n");
}

static void test_app_handles_resolve_to_root_widgets_for_overlays(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)rt_floatingpanel_new(&app);
    assert(panel);
    assert(panel->base.parent == app.root);

    void *palette_handle = rt_commandpalette_new(&app);
    assert(palette_handle);
    assert(app.command_palette_count == 1);
    assert(app.command_palettes[0] != NULL);

    rt_shortcuts_register(
        rt_const_cstr("palette"), rt_const_cstr("Ctrl+Shift+P"), rt_const_cstr(""));
    assert(app.shortcut_count == 1);
    assert(app.shortcuts != NULL);

    rt_shortcuts_clear();
    rt_commandpalette_destroy(palette_handle);
    cleanup_fake_app(&app);
    printf("test_app_handles_resolve_to_root_widgets_for_overlays: PASSED\n");
}

static void test_codeeditor_runtime_supports_multicursor_editing(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *editor = rt_codeeditor_new(app.root);
    assert(editor);

    assert(rt_codeeditor_get_tab_size(editor) == 4);
    rt_codeeditor_set_tab_size(editor, 12);
    assert(rt_codeeditor_get_tab_size(editor) == 12);
    rt_codeeditor_set_tab_size(editor, 99);
    assert(rt_codeeditor_get_tab_size(editor) == 16);
    assert(rt_codeeditor_get_word_wrap(editor) == 0);
    rt_codeeditor_set_word_wrap(editor, 1);
    assert(rt_codeeditor_get_word_wrap(editor) == 1);

    rt_codeeditor_set_text(editor, rt_const_cstr("abc\nabc"));
    rt_codeeditor_add_cursor(editor, 99, 99);
    assert(rt_codeeditor_get_cursor_count(editor) == 2);
    assert(rt_codeeditor_get_cursor_line_at(editor, 1) == 1);
    assert(rt_codeeditor_get_cursor_col_at(editor, 1) == 3);

    rt_codeeditor_set_cursor_selection(editor, 1, 1, 0, 1, 2);
    assert(rt_codeeditor_cursor_has_selection(editor, 1) == 1);

    rt_codeeditor_set_cursor_position_at(editor, 0, 0, 1);
    rt_codeeditor_set_cursor_position_at(editor, 1, 1, 1);
    assert(rt_codeeditor_cursor_has_selection(editor, 1) == 0);

    rt_codeeditor_insert_at_cursor(editor, rt_const_cstr("X"));
    assert(rt_codeeditor_can_undo(editor) == 1);
    assert(rt_codeeditor_can_redo(editor) == 0);
    rt_string text = rt_codeeditor_get_text(editor);
    assert(strcmp(rt_string_cstr(text), "aXbc\naXbc") == 0);

    rt_codeeditor_undo(editor);
    assert(rt_codeeditor_can_redo(editor) == 1);
    text = rt_codeeditor_get_text(editor);
    assert(strcmp(rt_string_cstr(text), "abc\nabc") == 0);

    cleanup_fake_app(&app);
    printf("test_codeeditor_runtime_supports_multicursor_editing: PASSED\n");
}

static void test_codeeditor_set_text_round_trips_embedded_nuls(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor);

    const char bytes[] = {'a', 'b', '\0', 'c', 'd'};
    rt_codeeditor_set_text(editor, rt_string_from_bytes(bytes, sizeof(bytes)));
    assert(editor->line_count == 1);
    assert(editor->lines[0].length == sizeof(bytes));
    assert(memcmp(editor->lines[0].text, bytes, sizeof(bytes)) == 0);

    rt_string text = rt_codeeditor_get_text(editor);
    assert(rt_str_len(text) == (int64_t)sizeof(bytes));
    assert(memcmp(rt_string_cstr(text), bytes, sizeof(bytes)) == 0);

    vg_widget_destroy(&editor->base);
    printf("test_codeeditor_set_text_round_trips_embedded_nuls: PASSED\n");
}

static void test_codeeditor_runtime_pixel_helpers_follow_scroll_and_wrap(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor);

    vg_codeeditor_set_text(editor, "abcdefghi\nabcdef");
    editor->base.x = 100.0f;
    editor->base.y = 50.0f;
    editor->base.width = 70.0f;
    editor->base.height = 20.0f;
    editor->char_width = 10.0f;
    editor->line_height = 10.0f;
    editor->gutter_width = 20.0f;

    editor->cursor_line = 1;
    editor->cursor_col = 4;
    editor->scroll_x = 15.0f;
    editor->scroll_y = 5.0f;
    assert(rt_codeeditor_get_cursor_pixel_x(editor) == 145);
    assert(rt_codeeditor_get_cursor_pixel_y(editor) == 55);
    assert(rt_codeeditor_get_line_at_pixel(editor, 55) == 1);
    assert(rt_codeeditor_get_col_at_pixel(editor, 145, 55) == 4);

    rt_codeeditor_set_word_wrap(editor, 1);
    editor->scroll_y = 10.0f;
    editor->cursor_line = 0;
    editor->cursor_col = 6;
    assert(rt_codeeditor_get_cursor_pixel_x(editor) == 120);
    assert(rt_codeeditor_get_cursor_pixel_y(editor) == 60);
    assert(rt_codeeditor_get_line_at_pixel(editor, 60) == 0);
    assert(rt_codeeditor_get_line_at_pixel(editor, 70) == 1);
    assert(rt_codeeditor_get_col_at_pixel(editor, 140, 60) == 8);

    vg_widget_destroy(&editor->base);
    printf("test_codeeditor_runtime_pixel_helpers_follow_scroll_and_wrap: PASSED\n");
}

static void test_codeeditor_runtime_fold_helpers_skip_hidden_lines(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor);

    vg_codeeditor_set_text(editor, "one\ntwo\nthree\nfour");
    editor->base.x = 100.0f;
    editor->base.y = 50.0f;
    editor->base.width = 120.0f;
    editor->base.height = 40.0f;
    editor->char_width = 8.0f;
    editor->line_height = 10.0f;

    rt_codeeditor_set_show_line_numbers(editor, 0);
    rt_codeeditor_set_show_fold_gutter(editor, 1);
    rt_codeeditor_add_fold_region(editor, 0, 2);
    rt_codeeditor_fold(editor, 0);

    assert(editor->gutter_width > 0.0f);
    assert(rt_codeeditor_get_line_at_pixel(editor, 55) == 0);
    assert(rt_codeeditor_get_line_at_pixel(editor, 65) == 3);

    vg_codeeditor_set_cursor(editor, 2, 2);
    assert(editor->cursor_line == 0);

    vg_widget_destroy(&editor->base);
    printf("test_codeeditor_runtime_fold_helpers_skip_hidden_lines: PASSED\n");
}

static void test_codeeditor_line_number_width_override_tracks_character_width(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor);

    editor->char_width = 8.0f;
    rt_codeeditor_set_line_number_width(editor, 4);
    assert(editor->gutter_width == 32.0f);

    editor->char_width = 12.0f;
    vg_codeeditor_refresh_layout_state(editor);
    assert(editor->gutter_width == 48.0f);

    vg_widget_destroy(&editor->base);
    printf("test_codeeditor_line_number_width_override_tracks_character_width: PASSED\n");
}

static void test_zia_block_comment_highlighting_is_render_order_independent(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor);

    rt_codeeditor_set_text(editor, rt_const_cstr("/*\ninside\n*/\nlet value = 1"));
    rt_codeeditor_set_language(editor, rt_const_cstr("zia"));
    rt_codeeditor_set_token_color(editor, 0, 0xFF222222);
    rt_codeeditor_set_token_color(editor, 4, 0xFF111111);

    uint32_t inside_colors[16] = {0};
    editor->syntax_highlighter(
        &editor->base, 1, editor->lines[1].text, inside_colors, editor->syntax_data);
    for (size_t i = 0; i < editor->lines[1].length; i++)
        assert(inside_colors[i] == 0xFF111111);

    uint32_t after_colors[32] = {0};
    editor->syntax_highlighter(
        &editor->base, 3, editor->lines[3].text, after_colors, editor->syntax_data);
    assert(after_colors[0] != 0xFF111111);

    vg_widget_destroy(&editor->base);
    printf("test_zia_block_comment_highlighting_is_render_order_independent: PASSED\n");
}

static void test_zia_string_highlighting_trailing_escape_stays_in_bounds(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor);
    rt_codeeditor_set_text(editor, rt_const_cstr("\"abc\\"));
    rt_codeeditor_set_language(editor, rt_const_cstr("zia"));
    rt_codeeditor_set_token_color(editor, 3, 0xFF123456);

    uint32_t colors[6] = {0};
    colors[5] = 0xDEADBEEF;
    editor->syntax_highlighter(
        &editor->base, 0, editor->lines[0].text, colors, editor->syntax_data);
    assert(colors[5] == 0xDEADBEEF);
    for (size_t i = 0; i < editor->lines[0].length; i++)
        assert(colors[i] == 0xFF123456);

    vg_widget_destroy(&editor->base);
    printf("test_zia_string_highlighting_trailing_escape_stays_in_bounds: PASSED\n");
}

static void test_findbar_runtime_reads_live_text_and_reports_noop_replace(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *editor = rt_codeeditor_new(app.root);
    void *bar = rt_findbar_new(app.root);
    assert(editor);
    assert(bar);

    rt_codeeditor_set_text(editor, rt_const_cstr("alpha beta"));
    rt_findbar_bind_editor(bar, editor);
    rt_findbar_set_find_text(bar, rt_const_cstr("alpha"));
    rt_findbar_set_replace_text(bar, rt_const_cstr("omega"));

    rt_findbar_data_view_t *view = (rt_findbar_data_view_t *)bar;
    vg_textinput_set_text((vg_textinput_t *)view->bar->find_input, "beta");
    vg_textinput_set_text((vg_textinput_t *)view->bar->replace_input, "theta");

    assert(strcmp(rt_string_cstr(rt_findbar_get_find_text(bar)), "beta") == 0);
    assert(strcmp(rt_string_cstr(rt_findbar_get_replace_text(bar)), "theta") == 0);

    rt_findbar_set_find_text(bar, rt_const_cstr("missing"));
    assert(rt_findbar_find_next(bar) == 0);
    assert(rt_findbar_replace(bar) == 0);

    cleanup_fake_app(&app);
    printf("test_findbar_runtime_reads_live_text_and_reports_noop_replace: PASSED\n");
}

static void test_menu_and_toolbar_pixel_icons_become_image_icons(void) {
    void *pixels = rt_pixels_new(1, 1);
    assert(pixels);
    rt_pixels_fill(pixels, 0x11223344);

    vg_menubar_t *menubar = vg_menubar_create(NULL);
    assert(menubar);
    vg_menu_t *menu = vg_menubar_add_menu(menubar, "File");
    assert(menu);
    vg_menu_item_t *item = vg_menu_add_item(menu, "Open", NULL, NULL, NULL);
    assert(item);

    rt_menuitem_set_icon(item, pixels);
    assert(item->icon.type == VG_ICON_IMAGE);

    vg_contextmenu_t *context_menu = vg_contextmenu_create();
    assert(context_menu);
    vg_menu_item_t *context_item = vg_contextmenu_add_item(context_menu, "Copy", NULL, NULL, NULL);
    assert(context_item);

    rt_menuitem_set_icon(context_item, pixels);
    assert(context_item->icon.type == VG_ICON_IMAGE);
    assert(context_item->owner_contextmenu == context_menu);

    vg_toolbar_t *toolbar = vg_toolbar_create(NULL, VG_TOOLBAR_HORIZONTAL);
    assert(toolbar);
    vg_toolbar_item_t *tool_item =
        vg_toolbar_add_button(toolbar, "open", NULL, vg_icon_from_glyph('O'), NULL, NULL);
    assert(tool_item);

    rt_toolbaritem_set_icon_pixels(tool_item, pixels);
    assert(tool_item->icon.type == VG_ICON_IMAGE);

    char icon_path[256];
#ifdef _WIN32
    snprintf(icon_path, sizeof(icon_path), "viper_gui_icon_test.bmp");
#else
    snprintf(icon_path, sizeof(icon_path), "/tmp/viper_gui_icon_%ld.bmp", (long)getpid());
#endif
    assert(rt_pixels_save_bmp(pixels, rt_const_cstr(icon_path)) == 1);
    vg_toolbar_item_t *path_item =
        (vg_toolbar_item_t *)rt_toolbar_add_button(toolbar, rt_const_cstr(icon_path), rt_const_cstr("path"));
    assert(path_item);
    assert(path_item->icon.type == VG_ICON_IMAGE);

    rt_toolbaritem_set_icon(path_item, rt_const_cstr(icon_path));
    assert(path_item->icon.type == VG_ICON_IMAGE);

#ifdef _WIN32
    remove(icon_path);
#else
    unlink(icon_path);
#endif

    vg_widget_destroy(&toolbar->base);
    vg_widget_destroy(&context_menu->base);
    vg_widget_destroy(&menubar->base);
    printf("test_menu_and_toolbar_pixel_icons_become_image_icons: PASSED\n");
}

static void test_splitpane_runtime_boolean_matches_horizontal_semantics(void) {
    vg_splitpane_t *horizontal = (vg_splitpane_t *)rt_splitpane_new(NULL, 1);
    vg_splitpane_t *vertical = (vg_splitpane_t *)rt_splitpane_new(NULL, 0);

    assert(horizontal);
    assert(vertical);
    assert(horizontal->direction == VG_SPLIT_HORIZONTAL);
    assert(vertical->direction == VG_SPLIT_VERTICAL);

    vg_widget_destroy(&horizontal->base);
    vg_widget_destroy(&vertical->base);
    printf("test_splitpane_runtime_boolean_matches_horizontal_semantics: PASSED\n");
}

static void test_tabbar_was_changed_tracks_real_active_tab_transitions(void) {
    vg_tabbar_t *tabbar = vg_tabbar_create(NULL);
    assert(tabbar);

    vg_tab_t *first = vg_tabbar_add_tab(tabbar, "first.zia", true);
    vg_tab_t *second = vg_tabbar_add_tab(tabbar, "second.zia", true);
    assert(first);
    assert(second);

    assert(rt_tabbar_was_changed(tabbar) == 0);
    rt_tabbar_set_active(tabbar, second);
    assert(rt_tabbar_was_changed(tabbar) == 1);
    assert(rt_tabbar_was_changed(tabbar) == 0);

    rt_tabbar_remove_tab(tabbar, second);
    assert(rt_tabbar_get_active(tabbar) == first);
    assert(rt_tabbar_was_changed(tabbar) == 1);
    assert(rt_tabbar_was_changed(tabbar) == 0);

    vg_widget_destroy(&tabbar->base);
    printf("test_tabbar_was_changed_tracks_real_active_tab_transitions: PASSED\n");
}

static void test_widget_set_position_marks_widget_dirty(void) {
    vg_widget_t *widget = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(widget);
    widget->needs_layout = false;
    widget->needs_paint = false;

    rt_widget_set_position(widget, 12, 34);
    assert(widget->x == 12.0f);
    assert(widget->y == 34.0f);
    assert(widget->manual_position);
    assert(!widget->needs_layout);
    assert(widget->needs_paint);

    vg_widget_destroy(widget);
    printf("test_widget_set_position_marks_widget_dirty: PASSED\n");
}

static void test_tabbar_close_click_index_survives_auto_close(void) {
    vg_tabbar_t *tabbar = vg_tabbar_create(NULL);
    assert(tabbar);

    vg_tab_t *tab = vg_tabbar_add_tab(tabbar, "main.zia", true);
    assert(tab);
    tabbar->base.x = 0.0f;
    tabbar->base.y = 0.0f;
    tabbar->base.width = 200.0f;
    tabbar->base.height = tabbar->tab_height;

    vg_event_t down = {0};
    down.type = VG_EVENT_MOUSE_DOWN;
    down.mouse.x = 180.0f;
    down.mouse.y = tabbar->tab_height / 2.0f;
    down.mouse.screen_x = down.mouse.x;
    down.mouse.screen_y = down.mouse.y;
    assert(vg_event_send(&tabbar->base, &down));

    vg_event_t up = down;
    up.type = VG_EVENT_MOUSE_UP;
    assert(vg_event_send(&tabbar->base, &up));

    assert(tabbar->tab_count == 0);
    assert(rt_tabbar_was_close_clicked(tabbar) == 1);
    assert(rt_tabbar_get_close_clicked_index(tabbar) == 0);
    assert(rt_tabbar_was_close_clicked(tabbar) == 0);
    assert(rt_tabbar_get_close_clicked_index(tabbar) == -1);

    vg_widget_destroy(&tabbar->base);
    printf("test_tabbar_close_click_index_survives_auto_close: PASSED\n");
}

static void test_widget_destroy_refuses_app_root_and_app_handle(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_widget_t *child = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(child);
    vg_widget_add_child(app.root, child);
    assert(app.root->child_count == 1);

    rt_widget_destroy(app.root);
    assert(app.root != NULL);
    assert(app.root->child_count == 1);

    rt_widget_destroy(&app);
    assert(app.root != NULL);
    assert(app.root->child_count == 1);

    cleanup_fake_app(&app);
    printf("test_widget_destroy_refuses_app_root_and_app_handle: PASSED\n");
}

static void test_widget_set_size_refuses_app_root(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    rt_widget_set_size(app.root, 320, 200);
    assert(app.root->constraints.min_width == 0.0f);
    assert(app.root->constraints.max_width == 0.0f);
    assert(app.root->constraints.preferred_width == 0.0f);
    assert(app.root->constraints.min_height == 0.0f);
    assert(app.root->constraints.max_height == 0.0f);
    assert(app.root->constraints.preferred_height == 0.0f);

    cleanup_fake_app(&app);
    printf("test_widget_set_size_refuses_app_root: PASSED\n");
}

static void test_widget_base_apis_reject_app_handles_and_invalid_children(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    rt_widget_set_visible(&app, 0);
    rt_widget_set_enabled(&app, 0);
    rt_widget_set_size(&app, 20, 30);
    rt_widget_set_flex(&app, 2.0);
    rt_widget_set_margin(&app, 5);
    rt_widget_set_tab_index(&app, 3);

    assert(rt_widget_is_visible(&app) == 0);
    assert(rt_widget_is_enabled(&app) == 0);
    assert(rt_widget_get_width(&app) == 0);
    assert(rt_widget_get_height(&app) == 0);
    assert(rt_widget_get_x(&app) == 0);
    assert(rt_widget_get_y(&app) == 0);
    assert(rt_widget_get_flex(&app) == 0.0);

    rt_widget_add_child(&app, &app);
    assert(app.root->child_count == 0);

    cleanup_fake_app(&app);
    printf("test_widget_base_apis_reject_app_handles_and_invalid_children: PASSED\n");
}

static void test_runtime_widget_parent_validation_rejects_leaf_and_invalid_handles(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_button_t *leaf_parent = (vg_button_t *)rt_button_new(app.root, rt_const_cstr("button"));
    assert(leaf_parent);
    vg_label_t *app_parented = (vg_label_t *)rt_label_new(&app, rt_const_cstr("app parent"));
    assert(app_parented && app_parented->base.parent == app.root);
    assert(rt_label_new(leaf_parent, rt_const_cstr("bad leaf parent")) == NULL);
    assert(rt_slider_new(leaf_parent, 1) == NULL);
    assert(rt_menubar_new(leaf_parent) == NULL);
    assert(rt_toolbar_new(leaf_parent) == NULL);
    assert(rt_statusbar_new(leaf_parent) == NULL);
    assert(rt_findbar_new(leaf_parent) == NULL);
    assert(rt_breadcrumb_new(leaf_parent) == NULL);
    assert(rt_minimap_new(leaf_parent) == NULL);
    assert(rt_floatingpanel_new(leaf_parent) == NULL);

    int invalid_handle = 0;
    assert(rt_button_new(&invalid_handle, rt_const_cstr("bad invalid parent")) == NULL);

    vg_label_t *detached = (vg_label_t *)rt_label_new(NULL, rt_const_cstr("detached"));
    assert(detached);
    assert(detached->base.parent == NULL);
    rt_widget_add_child(leaf_parent, detached);
    assert(detached->base.parent == NULL);
    rt_widget_add_child(app.root, detached);
    assert(detached->base.parent == app.root);

    cleanup_fake_app(&app);
    printf("test_runtime_widget_parent_validation_rejects_leaf_and_invalid_handles: PASSED\n");
}

static void test_widget_destroy_clears_nested_toolbar_statusbar_runtime_refs(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_widget_t *container = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(container);
    vg_widget_add_child(app.root, container);

    vg_statusbar_t *statusbar = vg_statusbar_create(container);
    assert(statusbar);
    vg_statusbar_item_t *status_item =
        vg_statusbar_add_text(statusbar, VG_STATUSBAR_ZONE_LEFT, "ready");
    assert(status_item);

    vg_toolbar_t *toolbar = vg_toolbar_create(container, VG_TOOLBAR_HORIZONTAL);
    assert(toolbar);
    vg_toolbar_item_t *tool_item =
        vg_toolbar_add_button(toolbar, "open", NULL, vg_icon_from_glyph('O'), NULL, NULL);
    assert(tool_item);

    app.last_statusbar_clicked = status_item;
    app.last_toolbar_clicked = tool_item;
    rt_widget_destroy(container);

    assert(app.last_statusbar_clicked == NULL);
    assert(app.last_toolbar_clicked == NULL);
    assert(app.root->child_count == 0);

    cleanup_fake_app(&app);
    printf("test_widget_destroy_clears_nested_toolbar_statusbar_runtime_refs: PASSED\n");
}

static void test_widget_focus_null_is_noop(void) {
    rt_widget_focus(NULL);
    printf("test_widget_focus_null_is_noop: PASSED\n");
}

static void test_image_set_pixels_converts_viper_pixels_to_rgba(void) {
    void *pixels = rt_pixels_new(1, 1);
    assert(pixels);
    rt_pixels_set(pixels, 0, 0, 0x11223344);

    vg_image_t *image = vg_image_create(NULL);
    assert(image);
    rt_image_set_pixels(image, pixels, 0, 0);

    assert(image->img_width == 1);
    assert(image->img_height == 1);
    assert(image->pixels);
    assert(image->pixels[0] == 0x11);
    assert(image->pixels[1] == 0x22);
    assert(image->pixels[2] == 0x33);
    assert(image->pixels[3] == 0x44);

    rt_image_set_pixels(image, NULL, 0, 0);
    assert(image->pixels == NULL);
    assert(image->img_width == 0);
    assert(image->img_height == 0);

    vg_widget_destroy(&image->base);
    printf("test_image_set_pixels_converts_viper_pixels_to_rgba: PASSED\n");
}

static void test_treeview_and_listbox_data_preserve_embedded_nuls(void) {
    char payload[] = {'a', '\0', 'b'};
    rt_string data = rt_string_from_bytes(payload, sizeof(payload));

    vg_treeview_t *tree = vg_treeview_create(NULL);
    assert(tree);
    vg_tree_node_t *node = vg_treeview_add_node(tree, NULL, "node");
    assert(node);
    rt_treeview_node_set_data(node, data);
    rt_string tree_data = rt_treeview_node_get_data(node);
    assert(rt_str_len(tree_data) == (int64_t)sizeof(payload));
    assert(memcmp(rt_string_cstr(tree_data), payload, sizeof(payload)) == 0);

    vg_listbox_t *listbox = vg_listbox_create(NULL);
    assert(listbox);
    vg_listbox_item_t *item = vg_listbox_add_item(listbox, "item", NULL);
    assert(item);
    rt_listbox_item_set_data(item, data);
    rt_string list_data = rt_listbox_item_get_data(item);
    assert(rt_str_len(list_data) == (int64_t)sizeof(payload));
    assert(memcmp(rt_string_cstr(list_data), payload, sizeof(payload)) == 0);
    assert(item->owns_user_data);

    vg_widget_destroy(&listbox->base);
    vg_widget_destroy(&tree->base);
    printf("test_treeview_and_listbox_data_preserve_embedded_nuls: PASSED\n");
}

static void test_listbox_selection_changed_is_edge_triggered(void) {
    vg_listbox_t *listbox = vg_listbox_create(NULL);
    assert(listbox);
    vg_listbox_item_t *first = vg_listbox_add_item(listbox, "first", NULL);
    vg_listbox_item_t *second = vg_listbox_add_item(listbox, "second", NULL);
    assert(first);
    assert(second);

    assert(rt_listbox_was_selection_changed(listbox) == 0);
    rt_listbox_select(listbox, first);
    assert(rt_listbox_was_selection_changed(listbox) == 1);
    assert(rt_listbox_was_selection_changed(listbox) == 0);

    rt_listbox_select(listbox, first);
    assert(rt_listbox_was_selection_changed(listbox) == 0);

    rt_listbox_select(listbox, second);
    assert(rt_listbox_was_selection_changed(listbox) == 1);
    assert(rt_listbox_was_selection_changed(listbox) == 0);

    vg_widget_destroy(&listbox->base);
    printf("test_listbox_selection_changed_is_edge_triggered: PASSED\n");
}

static void test_runtime_listbox_select_index_rejects_out_of_range_indices(void) {
    vg_listbox_t *listbox = vg_listbox_create(NULL);
    assert(listbox);
    vg_listbox_add_item(listbox, "first", NULL);
    vg_listbox_add_item(listbox, "second", NULL);

    rt_listbox_select_index(listbox, 1);
    assert(rt_listbox_get_selected_index(listbox) == 1);
    rt_listbox_select_index(listbox, INT64_MAX);
    assert(rt_listbox_get_selected_index(listbox) == 1);

    vg_listbox_set_virtual_mode(listbox, true, 2, 20.0f);
    rt_listbox_select_index(listbox, 1);
    assert(rt_listbox_get_selected_index(listbox) == 1);
    rt_listbox_select_index(listbox, 2);
    assert(rt_listbox_get_selected_index(listbox) == 1);

    vg_widget_destroy(&listbox->base);
    printf("test_runtime_listbox_select_index_rejects_out_of_range_indices: PASSED\n");
}

static void test_removed_listbox_and_treeview_handles_are_inert(void) {
    vg_listbox_t *listbox = vg_listbox_create(NULL);
    assert(listbox);
    void *item = rt_listbox_add_item(listbox, rt_const_cstr("item"));
    assert(item);
    rt_listbox_item_set_data(item, rt_const_cstr("payload"));
    rt_listbox_remove_item(listbox, item);
    assert(rt_str_len(rt_listbox_item_get_text(item)) == 0);
    assert(rt_str_len(rt_listbox_item_get_data(item)) == 0);
    rt_listbox_item_set_text(item, rt_const_cstr("ignored"));
    rt_listbox_select(listbox, item);
    assert(rt_listbox_get_selected(listbox) == NULL);

    vg_treeview_t *tree = vg_treeview_create(NULL);
    assert(tree);
    vg_tree_node_t *node = vg_treeview_add_node(tree, NULL, "node");
    assert(node);
    vg_tree_node_t *child = vg_treeview_add_node(tree, node, "child");
    assert(child);
    rt_treeview_node_set_data(child, rt_const_cstr("payload"));
    vg_treeview_remove_node(tree, node);
    assert(rt_str_len(rt_treeview_node_get_text(node)) == 0);
    assert(rt_str_len(rt_treeview_node_get_text(child)) == 0);
    assert(rt_str_len(rt_treeview_node_get_data(child)) == 0);
    rt_treeview_node_set_data(child, rt_const_cstr("ignored"));
    rt_treeview_select(tree, child);
    assert(rt_treeview_get_selected(tree) == NULL);
    assert(rt_treeview_node_is_expanded(child) == 0);

    vg_widget_destroy(&tree->base);
    vg_widget_destroy(&listbox->base);
    printf("test_removed_listbox_and_treeview_handles_are_inert: PASSED\n");
}

static void test_listbox_and_treeview_reject_foreign_child_handles(void) {
    vg_listbox_t *list_a = vg_listbox_create(NULL);
    vg_listbox_t *list_b = vg_listbox_create(NULL);
    assert(list_a && list_b);
    vg_listbox_item_t *item_b = (vg_listbox_item_t *)rt_listbox_add_item(list_b, rt_const_cstr("b"));
    assert(item_b);

    rt_listbox_select(list_a, item_b);
    assert(rt_listbox_get_selected(list_a) == NULL);
    rt_listbox_remove_item(list_a, item_b);
    assert(vg_listbox_item_is_live(item_b));
    assert(item_b->owner == list_b);
    assert(rt_listbox_get_count(list_b) == 1);

    vg_treeview_t *tree_a = vg_treeview_create(NULL);
    vg_treeview_t *tree_b = vg_treeview_create(NULL);
    assert(tree_a && tree_b);
    vg_tree_node_t *node_b = (vg_tree_node_t *)rt_treeview_add_node(
        tree_b, NULL, rt_const_cstr("b"));
    assert(node_b);

    assert(rt_treeview_add_node(tree_a, node_b, rt_const_cstr("bad child")) == NULL);
    rt_treeview_expand(tree_a, node_b);
    assert(node_b->expanded == false);
    rt_treeview_select(tree_a, node_b);
    assert(rt_treeview_get_selected(tree_a) == NULL);
    rt_treeview_remove_node(tree_a, node_b);
    assert(vg_tree_node_is_live(node_b));
    assert(node_b->owner == tree_b);

    vg_widget_destroy(&tree_b->base);
    vg_widget_destroy(&tree_a->base);
    vg_widget_destroy(&list_b->base);
    vg_widget_destroy(&list_a->base);
    printf("test_listbox_and_treeview_reject_foreign_child_handles: PASSED\n");
}

static void test_contextmenu_separator_returns_item_handle(void) {
    void *menu = rt_contextmenu_new();
    assert(menu);
    void *separator = rt_contextmenu_add_separator(menu);
    assert(separator);
    assert(rt_menuitem_is_separator(separator) == 1);

    rt_contextmenu_destroy(menu);
    printf("test_contextmenu_separator_returns_item_handle: PASSED\n");
}

static void test_contextmenu_item_click_updates_menuitem_was_clicked(void) {
    vg_contextmenu_t *menu = (vg_contextmenu_t *)rt_contextmenu_new();
    assert(menu);
    vg_menu_item_t *item = (vg_menu_item_t *)rt_contextmenu_add_item(menu, rt_const_cstr("Open"));
    assert(item);

    rt_contextmenu_show(menu, 0, 0);
    vg_event_t down = vg_event_mouse(VG_EVENT_MOUSE_DOWN, 4.0f, 4.0f, VG_MOUSE_LEFT, 0);
    assert(menu->base.vtable->handle_event(&menu->base, &down));

    assert(rt_contextmenu_get_clicked_item(menu) == item);
    assert(rt_menuitem_was_clicked(item) == 1);
    assert(rt_menuitem_was_clicked(item) == 0);

    rt_contextmenu_destroy(menu);
    printf("test_contextmenu_item_click_updates_menuitem_was_clicked: PASSED\n");
}

static void test_filedialog_show_without_active_window_returns_zero(void) {
    rt_gui_activate_app(NULL);
    void *dialog = rt_filedialog_new_open();
    assert(dialog);
    assert(rt_filedialog_show(dialog) == 0);
    assert(rt_filedialog_get_path_count(dialog) == 0);
    assert(rt_str_len(rt_filedialog_get_path(dialog)) == 0);
    rt_filedialog_destroy(dialog);

    vg_filedialog_t *raw_dialog = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    assert(raw_dialog);
    vg_widget_t *raw_widget = &raw_dialog->base.base;
    assert(vg_widget_is_live(raw_widget));
    vg_filedialog_destroy(raw_dialog);
    assert(!vg_widget_is_live(raw_widget));

    vg_widget_t *probe = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(probe);
    vg_widget_destroy(probe);

    printf("test_filedialog_show_without_active_window_returns_zero: PASSED\n");
}

static void test_filedialog_path_list_helpers_decode_escaped_paths(void) {
    rt_string escaped = rt_const_cstr("/a\\;b;/c\\\\d;tail\\");
    assert(rt_filedialog_path_list_count(escaped) == 3);

    rt_string first = rt_filedialog_path_list_get(escaped, 0);
    rt_string second = rt_filedialog_path_list_get(escaped, 1);
    rt_string third = rt_filedialog_path_list_get(escaped, 2);
    rt_string missing = rt_filedialog_path_list_get(escaped, 3);

    assert(strcmp(rt_string_cstr(first), "/a;b") == 0);
    assert(strcmp(rt_string_cstr(second), "/c\\d") == 0);
    assert(strcmp(rt_string_cstr(third), "tail\\") == 0);
    assert(rt_str_len(missing) == 0);
    assert(rt_filedialog_path_list_count(rt_str_empty()) == 0);

    printf("test_filedialog_path_list_helpers_decode_escaped_paths: PASSED\n");
}

static void test_commandpalette_methods_after_destroy_are_inert(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *palette = rt_commandpalette_new(&app);
    assert(palette);
    rt_commandpalette_add_command(
        palette, rt_const_cstr("open"), rt_const_cstr("Open"), rt_const_cstr("File"));
    rt_commandpalette_show(palette);
    assert(rt_commandpalette_is_visible(palette) == 1);
    rt_commandpalette_destroy(palette);
    assert(app.command_palette_count == 0);

    rt_commandpalette_add_command(
        palette, rt_const_cstr("save"), rt_const_cstr("Save"), rt_const_cstr("File"));
    rt_commandpalette_add_command_with_shortcut(palette,
                                                rt_const_cstr("close"),
                                                rt_const_cstr("Close"),
                                                rt_const_cstr("File"),
                                                rt_const_cstr("Ctrl+W"));
    rt_commandpalette_remove_command(palette, rt_const_cstr("save"));
    rt_commandpalette_clear(palette);
    rt_commandpalette_show(palette);
    rt_commandpalette_hide(palette);
    rt_commandpalette_set_placeholder(palette, rt_const_cstr("Run command"));
    assert(rt_commandpalette_is_visible(palette) == 0);
    assert(rt_commandpalette_was_command_selected(palette) == 0);
    assert(rt_str_len(rt_commandpalette_get_selected_command(palette)) == 0);

    cleanup_fake_app(&app);
    printf("test_commandpalette_methods_after_destroy_are_inert: PASSED\n");
}

static void test_commandpalette_show_clear_remove_drop_stale_selection(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *palette = rt_commandpalette_new(&app);
    assert(palette);
    rt_commandpalette_add_command(
        palette, rt_const_cstr("old"), rt_const_cstr("Old"), rt_const_cstr("File"));
    rt_commandpalette_add_command(
        palette, rt_const_cstr("keep"), rt_const_cstr("Keep"), rt_const_cstr("File"));

    rt_commandpalette_data_view_t *data = (rt_commandpalette_data_view_t *)palette;
    data->selected_command = test_strdup_local("old");
    assert(data->selected_command);
    data->was_selected = 1;

    rt_commandpalette_show(palette);
    assert(rt_commandpalette_was_command_selected(palette) == 0);
    assert(rt_str_len(rt_commandpalette_get_selected_command(palette)) == 0);

    data->selected_command = test_strdup_local("old");
    assert(data->selected_command);
    data->was_selected = 1;
    rt_commandpalette_remove_command(palette, rt_const_cstr("old"));
    assert(rt_commandpalette_was_command_selected(palette) == 0);
    assert(rt_str_len(rt_commandpalette_get_selected_command(palette)) == 0);

    data->selected_command = test_strdup_local("keep");
    assert(data->selected_command);
    data->was_selected = 1;
    rt_commandpalette_clear(palette);
    assert(rt_commandpalette_was_command_selected(palette) == 0);
    assert(rt_str_len(rt_commandpalette_get_selected_command(palette)) == 0);

    rt_commandpalette_destroy(palette);
    cleanup_fake_app(&app);
    printf("test_commandpalette_show_clear_remove_drop_stale_selection: PASSED\n");
}

static void test_commandpalette_rejects_embedded_nul_command_ids(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *palette = rt_commandpalette_new(&app);
    assert(palette);
    rt_commandpalette_data_view_t *data = (rt_commandpalette_data_view_t *)palette;
    assert(data->palette);

    const char bad_id[] = {'b', 'a', 'd', '\0', 'i', 'd'};
    rt_commandpalette_add_command(palette,
                                  rt_string_from_bytes(bad_id, sizeof(bad_id)),
                                  rt_const_cstr("Bad"),
                                  rt_const_cstr("File"));
    assert(data->palette->command_count == 0);

    rt_commandpalette_add_command_with_shortcut(palette,
                                                rt_string_from_bytes(bad_id, sizeof(bad_id)),
                                                rt_const_cstr("Bad"),
                                                rt_const_cstr("File"),
                                                rt_const_cstr("Ctrl+B"));
    assert(data->palette->command_count == 0);

    rt_commandpalette_destroy(palette);
    cleanup_fake_app(&app);
    printf("test_commandpalette_rejects_embedded_nul_command_ids: PASSED\n");
}

static void test_numeric_setters_sanitize_invalid_values(void) {
    vg_widget_t *widget = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(widget);
    rt_widget_set_size(widget, -5, INT64_MAX);
    assert(widget->constraints.preferred_width == 0.0f);
    assert(widget->constraints.preferred_height == (float)RT_GUI_MAX_LAYOUT_VALUE);

    rt_widget_set_flex(widget, NAN);
    assert(widget->layout.flex == 0.0f);
    rt_widget_set_margin(widget, -9);
    assert(widget->layout.margin_left == 0.0f);
    rt_widget_set_position(widget, INT64_MIN, INT64_MAX);
    assert(widget->x == (float)-RT_GUI_MAX_LAYOUT_VALUE);
    assert(widget->y == (float)RT_GUI_MAX_LAYOUT_VALUE);

    vg_splitpane_t *split = vg_splitpane_create(NULL, VG_SPLIT_HORIZONTAL);
    assert(split);
    rt_splitpane_set_position(split, NAN);
    assert(split->split_position == 0.5f);

    vg_slider_t *slider = vg_slider_create(NULL, VG_SLIDER_HORIZONTAL);
    assert(slider);
    rt_slider_set_range(slider, 10.0, -5.0);
    assert(slider->min_value == -5.0f);
    assert(slider->max_value == 10.0f);
    rt_slider_set_step(slider, -1.0);
    assert(slider->step == 0.0f);

    vg_image_t *image = vg_image_create(NULL);
    assert(image);
    rt_image_set_opacity(image, NAN);
    assert(image->opacity == 1.0f);
    rt_image_set_opacity(image, 2.0);
    assert(image->opacity == 1.0f);
    rt_image_set_opacity(image, -1.0);
    assert(image->opacity == 0.0f);

    vg_progressbar_t *progress = vg_progressbar_create(NULL);
    assert(progress);
    rt_progressbar_set_value(progress, NAN);
    assert(progress->value == 0.0f);
    rt_progressbar_set_value(progress, 2.0);
    assert(progress->value == 1.0f);

    vg_widget_destroy(&progress->base);
    vg_widget_destroy(&image->base);
    vg_widget_destroy(&slider->base);
    vg_widget_destroy(&split->base);
    vg_widget_destroy(widget);
    printf("test_numeric_setters_sanitize_invalid_values: PASSED\n");
}

static void test_font_destroy_defers_live_app_font(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.default_font = (vg_font_t *)0x5000;
    app.default_font_owned = 1;
    app.default_font_size = 14.0f;
    rt_gui_activate_app(&app);

    rt_font_destroy(app.default_font);
    assert(app.retired_font_count == 1);
    assert(app.retired_fonts[0] == app.default_font);

    app.default_font = NULL;
    cleanup_fake_app(&app);
    printf("test_font_destroy_defers_live_app_font: PASSED\n");
}

static void test_detached_widgets_do_not_inherit_current_app_font(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.default_font = (vg_font_t *)0x1;
    app.default_font_size = 23.0f;
    rt_gui_activate_app(&app);

    vg_label_t *label = (vg_label_t *)rt_label_new(NULL, rt_const_cstr("detached"));
    assert(label);
    assert(label->font != app.default_font);

    vg_widget_destroy(&label->base);
    cleanup_fake_app(&app);
    printf("test_detached_widgets_do_not_inherit_current_app_font: PASSED\n");
}

static void test_app_font_size_and_late_attach_reapply_default_font(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    app.default_font = (vg_font_t *)0x1;
    app.default_font_size = 12.0f;
    rt_gui_activate_app(&app);

    vg_label_t *attached = (vg_label_t *)rt_label_new(app.root, rt_const_cstr("attached"));
    assert(attached);
    assert(attached->font == app.default_font);
    assert(attached->font_size == 12.0f);
    rt_app_set_font_size(&app, 21.0);
    assert(attached->font == app.default_font);
    assert(attached->font_size == 21.0f);

    vg_label_t *detached = (vg_label_t *)rt_label_new(NULL, rt_const_cstr("detached"));
    assert(detached);
    assert(detached->font != app.default_font);
    rt_widget_add_child(app.root, detached);
    assert(detached->font == app.default_font);
    assert(detached->font_size == 21.0f);

    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)rt_floatingpanel_new(&app);
    assert(panel);
    vg_label_t *panel_child = (vg_label_t *)rt_label_new(NULL, rt_const_cstr("panel child"));
    assert(panel_child);
    assert(panel_child->font != app.default_font);
    rt_floatingpanel_add_child(panel, panel_child);
    assert(panel_child->font == app.default_font);
    assert(panel_child->font_size == 21.0f);

    cleanup_fake_app(&app);
    printf("test_app_font_size_and_late_attach_reapply_default_font: PASSED\n");
}

static void test_messagebox_show_after_destroy_returns_minus_one(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *box = rt_messagebox_new_info(rt_const_cstr("title"), rt_const_cstr("body"));
    assert(box);
    rt_messagebox_add_button(box, rt_const_cstr("OK"), 7);
    rt_messagebox_destroy(box);
    assert(rt_messagebox_show(box) == -1);

    cleanup_fake_app(&app);
    printf("test_messagebox_show_after_destroy_returns_minus_one: PASSED\n");
}

static void test_messagebox_one_shots_without_window_return_fallbacks(void) {
    rt_gui_activate_app(NULL);
    assert(rt_messagebox_info(rt_const_cstr("title"), rt_const_cstr("body")) == 0);
    assert(rt_messagebox_warning(rt_const_cstr("title"), rt_const_cstr("body")) == 0);
    assert(rt_messagebox_error(rt_const_cstr("title"), rt_const_cstr("body")) == 0);
    assert(rt_messagebox_question(rt_const_cstr("title"), rt_const_cstr("body")) == 0);
    assert(rt_messagebox_confirm(rt_const_cstr("title"), rt_const_cstr("body")) == 0);
    printf("test_messagebox_one_shots_without_window_return_fallbacks: PASSED\n");
}

static void test_toast_duration_is_clamped(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    rt_gui_activate_app(&app);

    void *negative = rt_toast_new(rt_const_cstr("negative"), RT_TOAST_INFO, -1);
    assert(negative);
    assert(app.notification_manager);
    assert(app.notification_manager->notification_count == 1);
    assert(app.notification_manager->notifications[0]->duration_ms == 0);

    void *huge = rt_toast_new(rt_const_cstr("huge"), RT_TOAST_INFO, (int64_t)UINT32_MAX + 99);
    assert(huge);
    assert(app.notification_manager->notification_count == 2);
    assert(app.notification_manager->notifications[1]->duration_ms == UINT32_MAX);

    rt_gui_features_cleanup(&app);
    rt_gui_activate_app(NULL);
    printf("test_toast_duration_is_clamped: PASSED\n");
}

static void test_toast_dismissal_is_edge_triggered_and_survives_cleanup(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    rt_gui_activate_app(&app);

    void *toast = rt_toast_new(rt_const_cstr("dismiss"), RT_TOAST_INFO, 0);
    assert(toast);
    rt_toast_dismiss(toast);
    assert(rt_toast_was_dismissed(toast) == 1);
    assert(rt_toast_was_dismissed(toast) == 0);

    void *stale = rt_toast_new(rt_const_cstr("cleanup"), RT_TOAST_INFO, 0);
    assert(stale);
    rt_gui_features_cleanup(&app);
    assert(rt_toast_was_dismissed(stale) == 1);
    assert(rt_toast_was_dismissed(stale) == 0);

    rt_gui_activate_app(NULL);
    printf("test_toast_dismissal_is_edge_triggered_and_survives_cleanup: PASSED\n");
}

static void test_breadcrumb_set_path_uses_literal_separator(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *crumb = rt_breadcrumb_new(&app);
    assert(crumb);
    rt_breadcrumb_set_path(crumb, rt_const_cstr("alpha::beta::gamma"), rt_const_cstr("::"));

    rt_breadcrumb_data_view_t *view = (rt_breadcrumb_data_view_t *)crumb;
    assert(view->breadcrumb);
    assert(view->breadcrumb->item_count == 3);
    assert(strcmp(view->breadcrumb->items[0].label, "alpha") == 0);
    assert(strcmp(view->breadcrumb->items[1].label, "beta") == 0);
    assert(strcmp(view->breadcrumb->items[2].label, "gamma") == 0);

    rt_breadcrumb_destroy(crumb);
    cleanup_fake_app(&app);
    printf("test_breadcrumb_set_path_uses_literal_separator: PASSED\n");
}

static void test_breadcrumb_clicked_data_preserves_embedded_nuls(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *crumb = rt_breadcrumb_new(&app);
    assert(crumb);
    const char payload[] = {'a', '\0', 'b'};
    rt_breadcrumb_add_item(crumb,
                           rt_const_cstr("node"),
                           rt_string_from_bytes(payload, sizeof(payload)));

    rt_breadcrumb_data_view_t *view = (rt_breadcrumb_data_view_t *)crumb;
    assert(view->breadcrumb && view->breadcrumb->item_count == 1);
    assert(view->breadcrumb->on_click);
    view->breadcrumb->on_click(view->breadcrumb, 0, view->breadcrumb->user_data);

    assert(rt_breadcrumb_was_item_clicked(crumb) == 1);
    assert(rt_breadcrumb_get_clicked_index(crumb) == 0);
    rt_string clicked = rt_breadcrumb_get_clicked_data(crumb);
    assert(rt_str_len(clicked) == (int64_t)sizeof(payload));
    assert(memcmp(rt_string_cstr(clicked), payload, sizeof(payload)) == 0);
    assert(rt_breadcrumb_was_item_clicked(crumb) == 0);

    rt_breadcrumb_destroy(crumb);
    cleanup_fake_app(&app);
    printf("test_breadcrumb_clicked_data_preserves_embedded_nuls: PASSED\n");
}

static void test_shortcuts_reject_invalid_bindings_atomically(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    s_current_app = &app;

    rt_shortcuts_register(rt_const_cstr("bad"), rt_const_cstr("Ctrl+NotAKey"), rt_const_cstr(""));
    assert(app.shortcut_count == 0);
    rt_shortcuts_register(rt_const_cstr("bad2"), rt_const_cstr("Ctrl+Bogus+S"), rt_const_cstr(""));
    assert(app.shortcut_count == 0);
    rt_shortcuts_register(rt_const_cstr("bad3"), rt_const_cstr("F1x"), rt_const_cstr(""));
    assert(app.shortcut_count == 0);

    rt_shortcuts_register(rt_const_cstr("save"), rt_const_cstr("Ctrl+S"), rt_const_cstr("save"));
    assert(app.shortcut_count == 1);
    assert(app.shortcuts[0].parsed_key == 'S');

    rt_shortcuts_register(rt_const_cstr("save"), rt_const_cstr("Ctrl+NotAKey"), rt_const_cstr("bad"));
    assert(app.shortcut_count == 1);
    assert(strcmp(app.shortcuts[0].keys, "Ctrl+S") == 0);
    assert(app.shortcuts[0].parsed_key == 'S');

    rt_shortcuts_register(rt_const_cstr("help"), rt_const_cstr("f5"), rt_const_cstr("help"));
    assert(app.shortcut_count == 2);
    assert(app.shortcuts[1].parsed_key == VG_KEY_F5);
    assert(rt_shortcuts_check_key(&app, VG_KEY_F5, 0) == 1);
    app.shortcuts_global_enabled = 0;
    assert(rt_shortcuts_was_triggered(rt_const_cstr("help")) == 1);
    assert(rt_shortcuts_check_key(&app, 'S', VG_MOD_CTRL) == 0);

    rt_shortcuts_clear();
    s_current_app = NULL;
    printf("test_shortcuts_reject_invalid_bindings_atomically: PASSED\n");
}

static void test_close_prevention_tracks_request_without_closing(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    rt_gui_activate_app(&app);

    rt_app_set_prevent_close(&app, 1);
    assert(app.prevent_close == 1);
    app.close_requested = 1;
    app.should_close = 0;
    assert(rt_app_was_close_requested(&app) == 1);
    assert(rt_gui_app_should_close(&app) == 0);

    rt_app_set_prevent_close(&app, 0);
    assert(app.prevent_close == 0);
    rt_gui_activate_app(NULL);
    printf("test_close_prevention_tracks_request_without_closing: PASSED\n");
}

static void test_menu_context_toolbar_statusbar_handles_are_type_checked(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    assert(rt_menubar_add_menu(&app, rt_const_cstr("Bad")) == NULL);
    vg_menubar_t *menubar = (vg_menubar_t *)rt_menubar_new(app.root);
    assert(menubar);
    vg_menu_t *menu = (vg_menu_t *)rt_menubar_add_menu(menubar, rt_const_cstr("File"));
    vg_menu_t *submenu = (vg_menu_t *)rt_menu_add_submenu(menu, rt_const_cstr("Recent"));
    assert(menu && submenu);
    rt_menubar_remove_menu(menubar, menu);
    assert(rt_menu_add_item(menu, rt_const_cstr("Open")) == NULL);
    assert(rt_menu_add_item(submenu, rt_const_cstr("Stale")) == NULL);
    assert(rt_menu_get_item_count(menu) == 0);

    assert(rt_contextmenu_add_item(&app, rt_const_cstr("Bad")) == NULL);
    void *context = rt_contextmenu_new();
    assert(context);
    rt_contextmenu_destroy(context);
    assert(rt_contextmenu_add_item(context, rt_const_cstr("Stale")) == NULL);

    vg_statusbar_t *statusbar = (vg_statusbar_t *)rt_statusbar_new(app.root);
    assert(statusbar);
    assert(rt_statusbar_add_text(&app, rt_const_cstr("bad"), 0) == NULL);
    assert(rt_statusbar_add_text(statusbar, rt_const_cstr("bad"), 99) == NULL);
    assert(statusbar->left_count == 0 && statusbar->center_count == 0 &&
           statusbar->right_count == 0);

    vg_toolbar_t *toolbar = (vg_toolbar_t *)rt_toolbar_new(app.root);
    assert(toolbar);
    assert(rt_toolbar_add_button(&app, rt_const_cstr(""), rt_const_cstr("")) == NULL);
    app.root->needs_layout = false;
    rt_toolbar_set_visible(toolbar, 0);
    assert(toolbar->base.visible == false);
    assert(app.root->needs_layout == true);

    uint64_t magic_before = app.magic;
    rt_container_set_spacing(&app, 4.0);
    rt_container_set_padding(&app, 4.0);
    assert(app.magic == magic_before);

    cleanup_fake_app(&app);
    printf("test_menu_context_toolbar_statusbar_handles_are_type_checked: PASSED\n");
}

static void test_toolbar_set_style_rejects_unknown_values(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_toolbar_t *toolbar = (vg_toolbar_t *)rt_toolbar_new(app.root);
    assert(toolbar);
    rt_toolbar_set_style(toolbar, RT_TOOLBAR_STYLE_ICON_ONLY);
    assert(toolbar->show_labels == false);
    rt_toolbar_set_style(toolbar, 99);
    assert(toolbar->show_labels == false);
    rt_toolbar_set_style(toolbar, RT_TOOLBAR_STYLE_ICON_TEXT);
    assert(toolbar->show_labels == true);
    rt_toolbar_set_style(toolbar, -1);
    assert(toolbar->show_labels == true);

    cleanup_fake_app(&app);
    printf("test_toolbar_set_style_rejects_unknown_values: PASSED\n");
}

static void test_floatingpanel_and_tabbar_reject_wrong_or_out_of_range_handles(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)rt_floatingpanel_new(&app);
    assert(panel);
    vg_label_t *label = (vg_label_t *)rt_label_new(NULL, rt_const_cstr("child"));
    assert(label);
    rt_floatingpanel_add_child(&app, label);
    assert(label->base.parent == NULL);
    rt_floatingpanel_add_child(panel, label);
    assert(label->base.parent == &panel->base);

    vg_tabbar_t *tabbar = (vg_tabbar_t *)rt_tabbar_new(app.root);
    assert(tabbar);
    rt_tabbar_add_tab(tabbar, rt_const_cstr("One"), 0);
    assert(rt_tabbar_get_tab_at(tabbar, -1) == NULL);
    assert(rt_tabbar_get_tab_at(tabbar, (int64_t)INT_MAX + 1) == NULL);
    assert(rt_tabbar_get_tab_at(tabbar, 1) == NULL);
    assert(rt_tabbar_get_tab_at(tabbar, 0) != NULL);

    cleanup_fake_app(&app);
    printf("test_floatingpanel_and_tabbar_reject_wrong_or_out_of_range_handles: PASSED\n");
}

static void test_breadcrumb_minimap_methods_after_destroy_are_inert(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *crumb = rt_breadcrumb_new(app.root);
    assert(crumb);
    rt_breadcrumb_destroy(crumb);
    rt_breadcrumb_set_path(crumb, rt_const_cstr("a/b"), rt_const_cstr("/"));
    rt_breadcrumb_set_visible(crumb, 1);
    assert(rt_breadcrumb_is_visible(crumb) == 0);
    assert(rt_breadcrumb_get_clicked_index(crumb) == -1);

    void *minimap = rt_minimap_new(app.root);
    assert(minimap);
    rt_minimap_destroy(minimap);
    rt_minimap_set_width(minimap, 200);
    rt_minimap_set_visible(minimap, 1);
    rt_minimap_add_marker(minimap, 1, 0xFF0000FF, 0);
    assert(rt_minimap_get_width(minimap) == 0);
    assert(rt_minimap_is_visible(minimap) == 0);

    cleanup_fake_app(&app);
    printf("test_breadcrumb_minimap_methods_after_destroy_are_inert: PASSED\n");
}

static void test_type_specific_widget_apis_reject_wrong_types(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_label_t *label = (vg_label_t *)rt_label_new(app.root, rt_const_cstr("label"));
    vg_button_t *button = (vg_button_t *)rt_button_new(app.root, rt_const_cstr("button"));
    assert(label && button);

    rt_label_set_text(button, rt_const_cstr("wrong"));
    rt_button_set_text(label, rt_const_cstr("wrong"));
    assert(strcmp(label->text, "label") == 0);
    assert(strcmp(button->text, "button") == 0);

    assert(rt_dropdown_add_item(label, rt_const_cstr("wrong")) == -1);
    rt_slider_set_value(label, 0.5);
    assert(rt_slider_get_value(label) == 0.0);

    uint64_t magic_before = app.magic;
    rt_widget_set_draggable(&app, 1);
    rt_widget_set_drag_data(&app, rt_const_cstr("text/plain"), rt_const_cstr("data"));
    assert(app.magic == magic_before);

    cleanup_fake_app(&app);
    printf("test_type_specific_widget_apis_reject_wrong_types: PASSED\n");
}

static void test_findbar_methods_after_destroy_are_inert(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *editor = rt_codeeditor_new(app.root);
    void *bar = rt_findbar_new(app.root);
    assert(editor && bar);
    rt_findbar_bind_editor(bar, editor);
    rt_findbar_destroy(bar);

    rt_findbar_bind_editor(bar, editor);
    rt_findbar_set_find_text(bar, rt_const_cstr("x"));
    rt_findbar_set_replace_text(bar, rt_const_cstr("y"));
    rt_findbar_set_case_sensitive(bar, 1);
    assert(rt_findbar_find_next(bar) == 0);
    assert(rt_findbar_replace(bar) == 0);
    assert(rt_findbar_get_match_count(bar) == 0);
    assert(rt_findbar_is_visible(bar) == 0);

    cleanup_fake_app(&app);
    printf("test_findbar_methods_after_destroy_are_inert: PASSED\n");
}

static void test_findbar_parent_destroy_disconnects_wrapper(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_widget_t *container = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(container);
    vg_widget_add_child(app.root, container);

    void *editor = rt_codeeditor_new(app.root);
    void *bar = rt_findbar_new(container);
    assert(editor && bar);
    rt_findbar_bind_editor(bar, editor);

    rt_findbar_data_view_t *view = (rt_findbar_data_view_t *)bar;
    assert(view->bar != NULL);
    rt_widget_destroy(container);
    assert(view->bar == NULL);

    rt_findbar_set_find_text(bar, rt_const_cstr("x"));
    rt_findbar_set_replace_text(bar, rt_const_cstr("y"));
    assert(rt_findbar_find_next(bar) == 0);
    assert(rt_findbar_replace(bar) == 0);
    assert(rt_findbar_get_match_count(bar) == 0);

    cleanup_fake_app(&app);
    printf("test_findbar_parent_destroy_disconnects_wrapper: PASSED\n");
}

static void test_radiogroup_runtime_handle_invalidates_after_destroy(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *group = rt_radiogroup_new();
    assert(group);
    vg_radiobutton_t *radio =
        (vg_radiobutton_t *)rt_radiobutton_new(app.root, rt_const_cstr("one"), group);
    assert(radio && radio->group != NULL);
    rt_radiogroup_destroy(group);
    assert(radio->group == NULL);
    assert(rt_radiobutton_new(app.root, rt_const_cstr("bad"), group) == NULL);
    rt_radiobutton_set_selected(radio, 1);
    assert(rt_radiobutton_is_selected(radio) == 1);

    cleanup_fake_app(&app);
    printf("test_radiogroup_runtime_handle_invalidates_after_destroy: PASSED\n");
}

static void test_filedialog_setters_after_destroy_are_inert(void) {
    void *dialog = rt_filedialog_new_open();
    assert(dialog);
    rt_filedialog_destroy(dialog);
    rt_filedialog_set_title(dialog, rt_const_cstr("title"));
    rt_filedialog_set_path(dialog, rt_const_cstr("/tmp"));
    rt_filedialog_set_filter(dialog, rt_const_cstr("Files"), rt_const_cstr("*"));
    rt_filedialog_add_filter(dialog, rt_const_cstr("Text"), rt_const_cstr("*.txt"));
    rt_filedialog_set_default_name(dialog, rt_const_cstr("out.txt"));
    rt_filedialog_set_multiple(dialog, 1);
    assert(rt_filedialog_show(dialog) == 0);
    assert(rt_filedialog_get_path_count(dialog) == 0);
    assert(strcmp(rt_string_cstr(rt_filedialog_get_path(dialog)), "") == 0);
    printf("test_filedialog_setters_after_destroy_are_inert: PASSED\n");
}

static void test_filedialog_destroy_removes_modal_stack_entry(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *dialog = rt_filedialog_new_open();
    assert(dialog);
    rt_filedialog_data_view_t *view = (rt_filedialog_data_view_t *)dialog;
    assert(view->dialog);

    vg_filedialog_show(view->dialog);
    rt_gui_push_dialog(&app, &view->dialog->base);
    assert(app.dialog_count == 1);
    assert(vg_widget_get_modal_root() == &view->dialog->base.base);

    rt_filedialog_destroy(dialog);
    assert(app.dialog_count == 0);
    assert(vg_widget_get_modal_root() == NULL);

    cleanup_fake_app(&app);
    printf("test_filedialog_destroy_removes_modal_stack_entry: PASSED\n");
}

static void test_messagebox_custom_button_ids_preserve_zero_and_i64(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    void *box = rt_messagebox_new_info(rt_const_cstr("title"), rt_const_cstr("body"));
    assert(box);
    int64_t large_id = ((int64_t)1 << 40) + 17;
    rt_messagebox_add_button(box, rt_const_cstr("Zero"), 0);
    rt_messagebox_add_button(box, rt_const_cstr("Large"), large_id);
    rt_messagebox_set_default_button(box, large_id);

    rt_messagebox_data_view_t *view = (rt_messagebox_data_view_t *)box;
    assert(view->custom_button_count == 2);
    assert(view->custom_button_ids[0] == 0);
    assert(view->custom_button_ids[1] == large_id);
    assert(view->custom_buttons[0].result == (vg_dialog_result_t)1);
    assert(view->custom_buttons[1].result == (vg_dialog_result_t)2);
    assert(view->custom_buttons[0].is_default == false);
    assert(view->custom_buttons[1].is_default == true);

    vg_dialog_set_custom_buttons(view->dialog, view->custom_buttons, view->custom_button_count);
    assert(view->dialog->custom_buttons[0].result == (vg_dialog_result_t)1);
    assert(view->dialog->custom_buttons[1].result == (vg_dialog_result_t)2);

    rt_messagebox_destroy(box);
    cleanup_fake_app(&app);
    printf("test_messagebox_custom_button_ids_preserve_zero_and_i64: PASSED\n");
}

static void test_menuitem_checkable_state_is_real_and_invalidates_context(void) {
    vg_contextmenu_t *context = vg_contextmenu_create();
    assert(context);
    vg_menu_item_t *context_item = vg_contextmenu_add_item(context, "Toggle", NULL, NULL, NULL);
    assert(context_item);
    assert(rt_menuitem_is_checkable(context_item) == 0);
    context->base.needs_layout = false;
    rt_menuitem_set_checkable(context_item, 1);
    assert(rt_menuitem_is_checkable(context_item) == 1);
    rt_menuitem_set_checked(context_item, 1);
    assert(rt_menuitem_is_checked(context_item) == 1);
    assert(context->base.needs_layout || context->base.needs_paint);
    rt_menuitem_set_checkable(context_item, 0);
    assert(rt_menuitem_is_checkable(context_item) == 0);
    assert(rt_menuitem_is_checked(context_item) == 0);

    vg_menubar_t *menubar = vg_menubar_create(NULL);
    assert(menubar);
    vg_menu_t *menu = vg_menubar_add_menu(menubar, "View");
    vg_menu_item_t *menu_item = vg_menu_add_item(menu, "Sidebar", NULL, NULL, NULL);
    assert(menu_item);
    assert(rt_menuitem_is_checkable(menu_item) == 0);
    rt_menuitem_set_checked(menu_item, 1);
    assert(rt_menuitem_is_checkable(menu_item) == 1);
    assert(rt_menuitem_is_checked(menu_item) == 1);

    vg_widget_destroy(&context->base);
    vg_widget_destroy(&menubar->base);
    printf("test_menuitem_checkable_state_is_real_and_invalidates_context: PASSED\n");
}

static void test_contextmenu_submenu_ownership_detaches_safely(void) {
    vg_contextmenu_t *parent = vg_contextmenu_create();
    vg_contextmenu_t *child = vg_contextmenu_create();
    assert(parent && child);
    vg_menu_item_t *item = vg_contextmenu_add_submenu(parent, "Child", child);
    assert(item);
    assert(item->submenu == (struct vg_menu *)child);
    assert(child->parent_item == item);

    vg_contextmenu_destroy(child);
    assert(item->submenu == NULL);
    vg_contextmenu_destroy(parent);

    parent = vg_contextmenu_create();
    child = vg_contextmenu_create();
    assert(parent && child);
    item = vg_contextmenu_add_submenu(parent, "Child", child);
    assert(item && item->submenu == (struct vg_menu *)child);
    vg_contextmenu_destroy(parent);

    printf("test_contextmenu_submenu_ownership_detaches_safely: PASSED\n");
}

static void test_toolbar_remove_item_removes_runtime_null_id_items(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_toolbar_t *toolbar = (vg_toolbar_t *)rt_toolbar_new(app.root);
    assert(toolbar);
    vg_toolbar_item_t *item =
        (vg_toolbar_item_t *)rt_toolbar_add_button(toolbar, rt_const_cstr(""), rt_const_cstr(""));
    assert(item && item->id == NULL);
    assert(toolbar->item_count == 1);
    rt_toolbar_remove_item(toolbar, item);
    assert(toolbar->item_count == 0);

    cleanup_fake_app(&app);
    printf("test_toolbar_remove_item_removes_runtime_null_id_items: PASSED\n");
}

static void test_toolbar_statusbar_remove_clears_runtime_click_caches(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_statusbar_t *statusbar = (vg_statusbar_t *)rt_statusbar_new(app.root);
    assert(statusbar);
    vg_statusbar_item_t *status_item =
        (vg_statusbar_item_t *)rt_statusbar_add_text(statusbar, rt_const_cstr("ready"), 0);
    assert(status_item);
    app.last_statusbar_clicked = status_item;
    rt_statusbar_remove_item(statusbar, status_item);
    assert(app.last_statusbar_clicked == NULL);
    assert(!vg_statusbar_item_is_live(status_item));
    assert(rt_statusbaritem_was_clicked(status_item) == 0);

    vg_toolbar_t *toolbar = (vg_toolbar_t *)rt_toolbar_new(app.root);
    assert(toolbar);
    vg_toolbar_item_t *tool_item =
        (vg_toolbar_item_t *)rt_toolbar_add_button(toolbar,
                                                   rt_const_cstr(""),
                                                   rt_const_cstr("tool"));
    assert(tool_item);
    app.last_toolbar_clicked = tool_item;
    rt_toolbar_remove_item(toolbar, tool_item);
    assert(app.last_toolbar_clicked == NULL);
    assert(!vg_toolbar_item_is_live(tool_item));
    assert(rt_toolbaritem_was_clicked(tool_item) == 0);

    cleanup_fake_app(&app);
    printf("test_toolbar_statusbar_remove_clears_runtime_click_caches: PASSED\n");
}

static void test_codeeditor_add_highlight_rejects_empty_and_inverted_spans(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_codeeditor_t *editor = (vg_codeeditor_t *)rt_codeeditor_new(app.root);
    assert(editor);
    rt_codeeditor_set_text(editor, rt_const_cstr("alpha\nbeta"));

    rt_codeeditor_add_highlight(editor, 0, 2, 0, 2, 0xFFFF0000);
    rt_codeeditor_add_highlight(editor, 1, 4, 0, 1, 0xFFFF0000);
    assert(editor->highlight_span_count == 0);

    rt_codeeditor_add_highlight(editor, 0, 0, 0, 5, 0xFF00FF00);
    assert(editor->highlight_span_count == 1);
    assert(editor->highlight_spans[0].start_line == 0);
    assert(editor->highlight_spans[0].end_col == 5);

    cleanup_fake_app(&app);
    printf("test_codeeditor_add_highlight_rejects_empty_and_inverted_spans: PASSED\n");
}

static void test_codeeditor_gutter_slots_are_validated_and_click_coords_are_fresh(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    assert(app.root);
    app.root->user_data = &app;
    rt_gui_activate_app(&app);

    vg_codeeditor_t *editor = (vg_codeeditor_t *)rt_codeeditor_new(app.root);
    assert(editor);

    rt_codeeditor_set_gutter_icon(editor, 0, NULL, 4);
    rt_codeeditor_set_gutter_icon(editor, 0, NULL, -1);
    assert(editor->gutter_icon_count == 0);

    rt_codeeditor_set_gutter_icon(editor, 0, NULL, 3);
    assert(editor->gutter_icon_count == 1);
    rt_codeeditor_clear_gutter_icon(editor, 0, -1);
    assert(editor->gutter_icon_count == 1);
    rt_codeeditor_clear_all_gutter_icons(editor, 4);
    assert(editor->gutter_icon_count == 1);
    rt_codeeditor_clear_gutter_icon(editor, 0, 3);
    assert(editor->gutter_icon_count == 0);

    editor->gutter_clicked = true;
    editor->gutter_clicked_line = 7;
    editor->gutter_clicked_slot = 2;
    assert(rt_codeeditor_get_gutter_clicked_line(editor) == 7);
    assert(rt_codeeditor_get_gutter_clicked_slot(editor) == 2);
    assert(rt_codeeditor_was_gutter_clicked(editor) == 1);
    assert(rt_codeeditor_was_gutter_clicked(editor) == 0);
    assert(rt_codeeditor_get_gutter_clicked_line(editor) == -1);
    assert(rt_codeeditor_get_gutter_clicked_slot(editor) == -1);

    cleanup_fake_app(&app);
    printf("test_codeeditor_gutter_slots_are_validated_and_click_coords_are_fresh: PASSED\n");
}

int main(void) {
    printf("=== GUI Runtime Regression Tests ===\n\n");

    test_shortcuts_are_app_scoped();
    test_file_drop_is_app_scoped();
    test_statusbar_click_is_edge_triggered();
    test_statusbar_runtime_button_wires_click_polling();
    test_default_font_is_applied_to_text_widgets();
    test_default_font_is_applied_to_complex_text_widgets();
    test_widget_set_font_rejects_stale_font_handles();
    test_dropdown_placeholder_is_copied();
    test_dialog_content_is_parented();
    test_notification_cleanup_runs_for_manual_dismiss();
    test_notification_ids_skip_zero_on_wrap();
    test_command_palette_placeholder_and_utf8_input();
    test_platform_text_events_translate_to_gui_text();
    test_platform_scroll_events_keep_screen_coordinates_separate();
    test_app_handles_resolve_to_root_widgets_for_overlays();
    test_codeeditor_runtime_supports_multicursor_editing();
    test_codeeditor_set_text_round_trips_embedded_nuls();
    test_codeeditor_runtime_pixel_helpers_follow_scroll_and_wrap();
    test_codeeditor_runtime_fold_helpers_skip_hidden_lines();
    test_codeeditor_line_number_width_override_tracks_character_width();
    test_zia_block_comment_highlighting_is_render_order_independent();
    test_zia_string_highlighting_trailing_escape_stays_in_bounds();
    test_splitpane_runtime_boolean_matches_horizontal_semantics();
    test_tabbar_was_changed_tracks_real_active_tab_transitions();
    test_widget_set_position_marks_widget_dirty();
    test_tabbar_close_click_index_survives_auto_close();
    test_findbar_runtime_reads_live_text_and_reports_noop_replace();
    test_menu_and_toolbar_pixel_icons_become_image_icons();
    test_widget_destroy_refuses_app_root_and_app_handle();
    test_widget_set_size_refuses_app_root();
    test_widget_base_apis_reject_app_handles_and_invalid_children();
    test_runtime_widget_parent_validation_rejects_leaf_and_invalid_handles();
    test_widget_destroy_clears_nested_toolbar_statusbar_runtime_refs();
    test_widget_focus_null_is_noop();
    test_image_set_pixels_converts_viper_pixels_to_rgba();
    test_treeview_and_listbox_data_preserve_embedded_nuls();
    test_listbox_selection_changed_is_edge_triggered();
    test_runtime_listbox_select_index_rejects_out_of_range_indices();
    test_removed_listbox_and_treeview_handles_are_inert();
    test_listbox_and_treeview_reject_foreign_child_handles();
    test_contextmenu_separator_returns_item_handle();
    test_contextmenu_item_click_updates_menuitem_was_clicked();
    test_filedialog_show_without_active_window_returns_zero();
    test_filedialog_path_list_helpers_decode_escaped_paths();
    test_commandpalette_methods_after_destroy_are_inert();
    test_commandpalette_show_clear_remove_drop_stale_selection();
    test_commandpalette_rejects_embedded_nul_command_ids();
    test_numeric_setters_sanitize_invalid_values();
    test_font_destroy_defers_live_app_font();
    test_detached_widgets_do_not_inherit_current_app_font();
    test_app_font_size_and_late_attach_reapply_default_font();
    test_messagebox_show_after_destroy_returns_minus_one();
    test_messagebox_one_shots_without_window_return_fallbacks();
    test_toast_duration_is_clamped();
    test_toast_dismissal_is_edge_triggered_and_survives_cleanup();
    test_breadcrumb_set_path_uses_literal_separator();
    test_breadcrumb_clicked_data_preserves_embedded_nuls();
    test_shortcuts_reject_invalid_bindings_atomically();
    test_close_prevention_tracks_request_without_closing();
    test_menu_context_toolbar_statusbar_handles_are_type_checked();
    test_toolbar_set_style_rejects_unknown_values();
    test_floatingpanel_and_tabbar_reject_wrong_or_out_of_range_handles();
    test_breadcrumb_minimap_methods_after_destroy_are_inert();
    test_type_specific_widget_apis_reject_wrong_types();
    test_findbar_methods_after_destroy_are_inert();
    test_findbar_parent_destroy_disconnects_wrapper();
    test_radiogroup_runtime_handle_invalidates_after_destroy();
    test_filedialog_setters_after_destroy_are_inert();
    test_filedialog_destroy_removes_modal_stack_entry();
    test_messagebox_custom_button_ids_preserve_zero_and_i64();
    test_menuitem_checkable_state_is_real_and_invalidates_context();
    test_contextmenu_submenu_ownership_detaches_safely();
    test_toolbar_remove_item_removes_runtime_null_id_items();
    test_toolbar_statusbar_remove_clears_runtime_click_caches();
    test_codeeditor_add_highlight_rejects_empty_and_inverted_spans();
    test_codeeditor_gutter_slots_are_validated_and_click_coords_are_fresh();

    printf("\nAll GUI runtime regression tests passed!\n");
    return 0;
}
