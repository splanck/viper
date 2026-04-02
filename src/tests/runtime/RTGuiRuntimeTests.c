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

#include "rt_gui.h"
#include "../../runtime/graphics/rt_gui_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void rt_gui_set_clicked_statusbar_item(void *item);

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

    assert(rt_app_was_file_dropped(&app_a) == 1);
    assert(rt_app_get_dropped_file_count(&app_a) == 1);
    assert(strcmp(rt_string_cstr(rt_app_get_dropped_file(&app_a, 0)), "/tmp/a.txt") == 0);

    assert(rt_app_was_file_dropped(&app_b) == 1);
    assert(rt_app_get_dropped_file_count(&app_b) == 1);
    assert(strcmp(rt_string_cstr(rt_app_get_dropped_file(&app_b, 0)), "/tmp/b.txt") == 0);

    assert(rt_app_was_file_dropped(&app_a) == 0);
    assert(rt_app_was_file_dropped(&app_b) == 0);

    rt_gui_features_cleanup(&app_a);
    rt_gui_features_cleanup(&app_b);

    printf("test_file_drop_is_app_scoped: PASSED\n");
}

static void test_statusbar_click_is_edge_triggered(void) {
    rt_gui_app_t app;
    vg_statusbar_item_t item;

    reset_fake_app(&app);
    memset(&item, 0, sizeof(item));
    s_current_app = &app;

    rt_gui_set_clicked_statusbar_item(&item);
    assert(rt_statusbaritem_was_clicked(&item) == 1);
    assert(rt_statusbaritem_was_clicked(&item) == 0);

    printf("test_statusbar_click_is_edge_triggered: PASSED\n");
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
    vg_radiogroup_t *group = vg_radiogroup_create();
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

    vg_radiogroup_destroy(group);
    cleanup_fake_app(&app);
    printf("test_default_font_is_applied_to_text_widgets: PASSED\n");
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

static void test_app_handles_resolve_to_root_widgets_for_overlays(void) {
    rt_gui_app_t app;
    reset_fake_app(&app);
    app.root = vg_widget_create(VG_WIDGET_CONTAINER);
    app.root->user_data = &app;
    s_current_app = &app;

    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)rt_floatingpanel_new(&app);
    assert(panel);
    assert(panel->base.parent == app.root);

    void *palette_handle = rt_commandpalette_new(&app);
    assert(palette_handle);
    assert(app.command_palette_count == 1);
    assert(app.command_palettes[0] != NULL);

    rt_shortcuts_register(rt_const_cstr("palette"), rt_const_cstr("Ctrl+Shift+P"), rt_const_cstr(""));
    assert(app.shortcut_count == 1);
    assert(app.shortcuts != NULL);

    rt_shortcuts_clear();
    rt_commandpalette_destroy(palette_handle);
    cleanup_fake_app(&app);
    printf("test_app_handles_resolve_to_root_widgets_for_overlays: PASSED\n");
}

int main(void) {
    printf("=== GUI Runtime Regression Tests ===\n\n");

    test_shortcuts_are_app_scoped();
    test_file_drop_is_app_scoped();
    test_statusbar_click_is_edge_triggered();
    test_default_font_is_applied_to_text_widgets();
    test_dropdown_placeholder_is_copied();
    test_dialog_content_is_parented();
    test_notification_cleanup_runs_for_manual_dismiss();
    test_command_palette_placeholder_and_utf8_input();
    test_platform_text_events_translate_to_gui_text();
    test_app_handles_resolve_to_root_widgets_for_overlays();

    printf("\nAll GUI runtime regression tests passed!\n");
    return 0;
}
