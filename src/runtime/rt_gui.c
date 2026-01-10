//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_gui.c
// Purpose: Runtime bridge implementation for ViperGUI widget library.
//
//===----------------------------------------------------------------------===//

#include "rt_gui.h"
#include "rt_string.h"

#include "../lib/gui/include/vg_font.h"
#include "../lib/gui/include/vg_widget.h"
#include "../lib/gui/include/vg_widgets.h"
#include "../lib/gui/include/vg_ide_widgets.h"
#include "../lib/gui/include/vg_theme.h"
#include "../lib/gui/include/vg_layout.h"
#include "../lib/graphics/include/vgfx.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Helper Functions
//=============================================================================

static char* rt_string_to_cstr(rt_string str) {
    if (!str) return NULL;
    size_t len = (size_t)rt_len(str);
    char* result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, rt_string_cstr(str), len);
    result[len] = '\0';
    return result;
}

//=============================================================================
// GUI Application
//=============================================================================

typedef struct {
    vgfx_window_t window;      // Underlying graphics window
    vg_widget_t* root;         // Root widget container
    vg_font_t* default_font;   // Default font for widgets
    float default_font_size;   // Default font size
    int64_t should_close;      // Close flag
    vg_widget_t* last_clicked; // Widget clicked this frame
    int32_t mouse_x;           // Current mouse X
    int32_t mouse_y;           // Current mouse Y
} rt_gui_app_t;

void* rt_gui_app_new(rt_string title, int64_t width, int64_t height) {
    rt_gui_app_t* app = calloc(1, sizeof(rt_gui_app_t));
    if (!app) return NULL;

    // Create window
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)width;
    params.height = (int32_t)height;
    char* ctitle = rt_string_to_cstr(title);
    if (ctitle) {
        params.title = ctitle;
    }
    params.resizable = 1;

    app->window = vgfx_create_window(&params);
    free(ctitle);

    if (!app->window) {
        free(app);
        return NULL;
    }

    // Create root container
    app->root = vg_widget_create(VG_WIDGET_CONTAINER);
    if (app->root) {
        vg_widget_set_fixed_size(app->root, (float)width, (float)height);
    }

    // Set dark theme by default
    vg_theme_set_current(vg_theme_dark());

    return app;
}

void rt_gui_app_destroy(void* app_ptr) {
    if (!app_ptr) return;
    rt_gui_app_t* app = (rt_gui_app_t*)app_ptr;

    if (app->root) {
        vg_widget_destroy(app->root);
    }
    if (app->window) {
        vgfx_destroy_window(app->window);
    }
    free(app);
}

int64_t rt_gui_app_should_close(void* app_ptr) {
    if (!app_ptr) return 1;
    rt_gui_app_t* app = (rt_gui_app_t*)app_ptr;
    return app->should_close;
}

// Forward declaration for widget rendering
static void render_widget_tree(vgfx_window_t window, vg_widget_t* widget, vg_font_t* font, float font_size);

void rt_gui_app_poll(void* app_ptr) {
    if (!app_ptr) return;
    rt_gui_app_t* app = (rt_gui_app_t*)app_ptr;
    if (!app->window) return;

    // Clear last clicked
    app->last_clicked = NULL;

    // Get mouse position
    vgfx_mouse_pos(app->window, &app->mouse_x, &app->mouse_y);

    // Poll events
    vgfx_event_t event;
    while (vgfx_poll_event(app->window, &event)) {
        if (event.type == VGFX_EVENT_CLOSE) {
            app->should_close = 1;
        }
        else if (event.type == VGFX_EVENT_MOUSE_MOVE) {
            app->mouse_x = event.data.mouse_move.x;
            app->mouse_y = event.data.mouse_move.y;
        }
        else if (event.type == VGFX_EVENT_MOUSE_UP) {
            // Find widget under mouse and mark as clicked
            if (app->root) {
                vg_widget_t* hit = vg_widget_hit_test(app->root, (float)app->mouse_x, (float)app->mouse_y);
                if (hit) {
                    app->last_clicked = hit;
                }
            }
        }
    }

    // Update widget hover states
    if (app->root) {
        // Clear all hover states first, then set the hovered one
        // (simplified - full implementation would traverse tree)
    }
}

void rt_gui_app_render(void* app_ptr) {
    if (!app_ptr) return;
    rt_gui_app_t* app = (rt_gui_app_t*)app_ptr;
    if (!app->window) return;

    // Clear with theme background
    vg_theme_t* theme = vg_theme_get_current();
    vgfx_cls(app->window, theme ? theme->colors.bg_secondary : 0xFF1E1E1E);

    // Render widget tree
    if (app->root) {
        render_widget_tree(app->window, app->root, app->default_font, app->default_font_size);
    }

    // Present
    vgfx_update(app->window);
}

void* rt_gui_app_get_root(void* app_ptr) {
    if (!app_ptr) return NULL;
    rt_gui_app_t* app = (rt_gui_app_t*)app_ptr;
    return app->root;
}

void rt_gui_app_set_font(void* app_ptr, void* font, double size) {
    if (!app_ptr) return;
    rt_gui_app_t* app = (rt_gui_app_t*)app_ptr;
    app->default_font = (vg_font_t*)font;
    app->default_font_size = (float)size;
}

// Simple widget rendering (would be expanded for full implementation)
static void render_widget_tree(vgfx_window_t window, vg_widget_t* widget, vg_font_t* font, float font_size) {
    if (!widget || !widget->visible) return;

    float x = widget->x;
    float y = widget->y;
    float w = widget->width;
    float h = widget->height;

    vg_theme_t* theme = vg_theme_get_current();
    if (!theme) return;

    // Render based on widget type
    switch (widget->type) {
        case VG_WIDGET_LABEL: {
            vg_label_t* label = (vg_label_t*)widget;
            if (label->font && label->text) {
                vg_font_draw_text(window, label->font, label->font_size,
                    x, y + label->font_size, label->text, label->text_color);
            } else if (font && label->text) {
                vg_font_draw_text(window, font, font_size,
                    x, y + font_size, label->text, label->text_color);
            }
            break;
        }
        case VG_WIDGET_BUTTON: {
            vg_button_t* btn = (vg_button_t*)widget;
            uint32_t bg = theme->colors.bg_primary;
            if (widget->state & VG_STATE_HOVERED) bg = theme->colors.bg_tertiary;
            if (widget->state & VG_STATE_PRESSED) bg = theme->colors.accent_primary;
            vgfx_rect(window, (int)x, (int)y, (int)w, (int)h, bg);
            if (btn->font && btn->text) {
                float tw = strlen(btn->text) * btn->font_size * 0.6f;
                float tx = x + (w - tw) / 2;
                float ty = y + (h + btn->font_size) / 2 - 2;
                vg_font_draw_text(window, btn->font, btn->font_size, tx, ty, btn->text, theme->colors.fg_primary);
            }
            break;
        }
        default:
            break;
    }

    // Render children
    vg_widget_t* child = widget->first_child;
    while (child) {
        render_widget_tree(window, child, font, font_size);
        child = child->next_sibling;
    }
}

//=============================================================================
// Font Functions
//=============================================================================

void* rt_font_load(rt_string path) {
    char* cpath = rt_string_to_cstr(path);
    if (!cpath) return NULL;

    vg_font_t* font = vg_font_load_file(cpath);
    free(cpath);
    return font;
}

void rt_font_destroy(void* font) {
    if (font) {
        vg_font_destroy((vg_font_t*)font);
    }
}

//=============================================================================
// Widget Functions
//=============================================================================

void rt_widget_destroy(void* widget) {
    if (widget) {
        vg_widget_destroy((vg_widget_t*)widget);
    }
}

void rt_widget_set_visible(void* widget, int64_t visible) {
    if (widget) {
        vg_widget_set_visible((vg_widget_t*)widget, visible != 0);
    }
}

void rt_widget_set_enabled(void* widget, int64_t enabled) {
    if (widget) {
        vg_widget_set_enabled((vg_widget_t*)widget, enabled != 0);
    }
}

void rt_widget_set_size(void* widget, int64_t width, int64_t height) {
    if (widget) {
        vg_widget_set_fixed_size((vg_widget_t*)widget, (float)width, (float)height);
    }
}

void rt_widget_add_child(void* parent, void* child) {
    if (parent && child) {
        vg_widget_add_child((vg_widget_t*)parent, (vg_widget_t*)child);
    }
}

//=============================================================================
// Label Widget
//=============================================================================

void* rt_label_new(void* parent, rt_string text) {
    char* ctext = rt_string_to_cstr(text);
    vg_label_t* label = vg_label_create((vg_widget_t*)parent, ctext);
    free(ctext);
    return label;
}

void rt_label_set_text(void* label, rt_string text) {
    if (!label) return;
    char* ctext = rt_string_to_cstr(text);
    vg_label_set_text((vg_label_t*)label, ctext);
    free(ctext);
}

void rt_label_set_font(void* label, void* font, double size) {
    if (label) {
        vg_label_set_font((vg_label_t*)label, (vg_font_t*)font, (float)size);
    }
}

void rt_label_set_color(void* label, int64_t color) {
    if (label) {
        vg_label_set_color((vg_label_t*)label, (uint32_t)color);
    }
}

//=============================================================================
// Button Widget
//=============================================================================

void* rt_button_new(void* parent, rt_string text) {
    char* ctext = rt_string_to_cstr(text);
    vg_button_t* button = vg_button_create((vg_widget_t*)parent, ctext);
    free(ctext);
    return button;
}

void rt_button_set_text(void* button, rt_string text) {
    if (!button) return;
    char* ctext = rt_string_to_cstr(text);
    vg_button_set_text((vg_button_t*)button, ctext);
    free(ctext);
}

void rt_button_set_font(void* button, void* font, double size) {
    if (button) {
        vg_button_set_font((vg_button_t*)button, (vg_font_t*)font, (float)size);
    }
}

void rt_button_set_style(void* button, int64_t style) {
    if (button) {
        vg_button_set_style((vg_button_t*)button, (vg_button_style_t)style);
    }
}

//=============================================================================
// TextInput Widget
//=============================================================================

void* rt_textinput_new(void* parent) {
    return vg_textinput_create((vg_widget_t*)parent);
}

void rt_textinput_set_text(void* input, rt_string text) {
    if (!input) return;
    char* ctext = rt_string_to_cstr(text);
    vg_textinput_set_text((vg_textinput_t*)input, ctext);
    free(ctext);
}

rt_string rt_textinput_get_text(void* input) {
    if (!input) return rt_str_empty();
    const char* text = vg_textinput_get_text((vg_textinput_t*)input);
    if (!text) return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_textinput_set_placeholder(void* input, rt_string placeholder) {
    if (!input) return;
    char* ctext = rt_string_to_cstr(placeholder);
    vg_textinput_set_placeholder((vg_textinput_t*)input, ctext);
    free(ctext);
}

void rt_textinput_set_font(void* input, void* font, double size) {
    if (input) {
        vg_textinput_set_font((vg_textinput_t*)input, (vg_font_t*)font, (float)size);
    }
}

//=============================================================================
// Checkbox Widget
//=============================================================================

void* rt_checkbox_new(void* parent, rt_string text) {
    char* ctext = rt_string_to_cstr(text);
    vg_checkbox_t* checkbox = vg_checkbox_create((vg_widget_t*)parent, ctext);
    free(ctext);
    return checkbox;
}

void rt_checkbox_set_checked(void* checkbox, int64_t checked) {
    if (checkbox) {
        vg_checkbox_set_checked((vg_checkbox_t*)checkbox, checked != 0);
    }
}

int64_t rt_checkbox_is_checked(void* checkbox) {
    if (!checkbox) return 0;
    return vg_checkbox_is_checked((vg_checkbox_t*)checkbox) ? 1 : 0;
}

void rt_checkbox_set_text(void* checkbox, rt_string text) {
    if (!checkbox) return;
    char* ctext = rt_string_to_cstr(text);
    vg_checkbox_set_text((vg_checkbox_t*)checkbox, ctext);
    free(ctext);
}

//=============================================================================
// ScrollView Widget
//=============================================================================

void* rt_scrollview_new(void* parent) {
    return vg_scrollview_create((vg_widget_t*)parent);
}

void rt_scrollview_set_scroll(void* scroll, double x, double y) {
    if (scroll) {
        vg_scrollview_set_scroll((vg_scrollview_t*)scroll, (float)x, (float)y);
    }
}

void rt_scrollview_set_content_size(void* scroll, double width, double height) {
    if (scroll) {
        vg_scrollview_set_content_size((vg_scrollview_t*)scroll, (float)width, (float)height);
    }
}

//=============================================================================
// TreeView Widget
//=============================================================================

void* rt_treeview_new(void* parent) {
    return vg_treeview_create((vg_widget_t*)parent);
}

void* rt_treeview_add_node(void* tree, void* parent_node, rt_string text) {
    if (!tree) return NULL;
    char* ctext = rt_string_to_cstr(text);
    vg_tree_node_t* node = vg_treeview_add_node((vg_treeview_t*)tree,
                                                  (vg_tree_node_t*)parent_node, ctext);
    free(ctext);
    return node;
}

void rt_treeview_remove_node(void* tree, void* node) {
    if (tree && node) {
        vg_treeview_remove_node((vg_treeview_t*)tree, (vg_tree_node_t*)node);
    }
}

void rt_treeview_clear(void* tree) {
    if (tree) {
        vg_treeview_clear((vg_treeview_t*)tree);
    }
}

void rt_treeview_expand(void* tree, void* node) {
    if (tree && node) {
        vg_treeview_expand((vg_treeview_t*)tree, (vg_tree_node_t*)node);
    }
}

void rt_treeview_collapse(void* tree, void* node) {
    if (tree && node) {
        vg_treeview_collapse((vg_treeview_t*)tree, (vg_tree_node_t*)node);
    }
}

void rt_treeview_select(void* tree, void* node) {
    if (tree) {
        vg_treeview_select((vg_treeview_t*)tree, (vg_tree_node_t*)node);
    }
}

void rt_treeview_set_font(void* tree, void* font, double size) {
    if (tree) {
        vg_treeview_set_font((vg_treeview_t*)tree, (vg_font_t*)font, (float)size);
    }
}

//=============================================================================
// TabBar Widget
//=============================================================================

void* rt_tabbar_new(void* parent) {
    return vg_tabbar_create((vg_widget_t*)parent);
}

void* rt_tabbar_add_tab(void* tabbar, rt_string title, int64_t closable) {
    if (!tabbar) return NULL;
    char* ctitle = rt_string_to_cstr(title);
    vg_tab_t* tab = vg_tabbar_add_tab((vg_tabbar_t*)tabbar, ctitle, closable != 0);
    free(ctitle);
    return tab;
}

void rt_tabbar_remove_tab(void* tabbar, void* tab) {
    if (tabbar && tab) {
        vg_tabbar_remove_tab((vg_tabbar_t*)tabbar, (vg_tab_t*)tab);
    }
}

void rt_tabbar_set_active(void* tabbar, void* tab) {
    if (tabbar) {
        vg_tabbar_set_active((vg_tabbar_t*)tabbar, (vg_tab_t*)tab);
    }
}

void rt_tab_set_title(void* tab, rt_string title) {
    if (!tab) return;
    char* ctitle = rt_string_to_cstr(title);
    vg_tab_set_title((vg_tab_t*)tab, ctitle);
    free(ctitle);
}

void rt_tab_set_modified(void* tab, int64_t modified) {
    if (tab) {
        vg_tab_set_modified((vg_tab_t*)tab, modified != 0);
    }
}

//=============================================================================
// SplitPane Widget
//=============================================================================

void* rt_splitpane_new(void* parent, int64_t horizontal) {
    vg_split_direction_t direction = horizontal ? VG_SPLIT_HORIZONTAL : VG_SPLIT_VERTICAL;
    return vg_splitpane_create((vg_widget_t*)parent, direction);
}

void rt_splitpane_set_position(void* split, double position) {
    if (split) {
        vg_splitpane_set_position((vg_splitpane_t*)split, (float)position);
    }
}

void* rt_splitpane_get_first(void* split) {
    if (!split) return NULL;
    return vg_splitpane_get_first((vg_splitpane_t*)split);
}

void* rt_splitpane_get_second(void* split) {
    if (!split) return NULL;
    return vg_splitpane_get_second((vg_splitpane_t*)split);
}

//=============================================================================
// CodeEditor Widget
//=============================================================================

void* rt_codeeditor_new(void* parent) {
    return vg_codeeditor_create((vg_widget_t*)parent);
}

void rt_codeeditor_set_text(void* editor, rt_string text) {
    if (!editor) return;
    char* ctext = rt_string_to_cstr(text);
    vg_codeeditor_set_text((vg_codeeditor_t*)editor, ctext);
    free(ctext);
}

rt_string rt_codeeditor_get_text(void* editor) {
    if (!editor) return rt_str_empty();
    char* text = vg_codeeditor_get_text((vg_codeeditor_t*)editor);
    if (!text) return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

void rt_codeeditor_set_cursor(void* editor, int64_t line, int64_t col) {
    if (editor) {
        vg_codeeditor_set_cursor((vg_codeeditor_t*)editor, (int)line, (int)col);
    }
}

void rt_codeeditor_scroll_to_line(void* editor, int64_t line) {
    if (editor) {
        vg_codeeditor_scroll_to_line((vg_codeeditor_t*)editor, (int)line);
    }
}

int64_t rt_codeeditor_get_line_count(void* editor) {
    if (!editor) return 0;
    return vg_codeeditor_get_line_count((vg_codeeditor_t*)editor);
}

int64_t rt_codeeditor_is_modified(void* editor) {
    if (!editor) return 0;
    return vg_codeeditor_is_modified((vg_codeeditor_t*)editor) ? 1 : 0;
}

void rt_codeeditor_clear_modified(void* editor) {
    if (editor) {
        vg_codeeditor_clear_modified((vg_codeeditor_t*)editor);
    }
}

void rt_codeeditor_set_font(void* editor, void* font, double size) {
    if (editor) {
        vg_codeeditor_set_font((vg_codeeditor_t*)editor, (vg_font_t*)font, (float)size);
    }
}

//=============================================================================
// Theme Functions
//=============================================================================

void rt_theme_set_dark(void) {
    vg_theme_set_current(vg_theme_dark());
}

void rt_theme_set_light(void) {
    vg_theme_set_current(vg_theme_light());
}

//=============================================================================
// Layout Functions
//=============================================================================

void* rt_vbox_new(void) {
    vg_widget_t* container = vg_widget_create(VG_WIDGET_CONTAINER);
    if (container) {
        // Set layout to VBox using layout data
        // For now this is a simple container, layout is set on the container
    }
    return container;
}

void* rt_hbox_new(void) {
    vg_widget_t* container = vg_widget_create(VG_WIDGET_CONTAINER);
    if (container) {
        // Set layout to HBox using layout data
    }
    return container;
}

void rt_container_set_spacing(void* container, double spacing) {
    if (container) {
        // Set spacing in layout params
        vg_widget_t* w = (vg_widget_t*)container;
        // The spacing would be stored in layout data
        (void)w;
        (void)spacing;
    }
}

void rt_container_set_padding(void* container, double padding) {
    if (container) {
        vg_widget_set_padding((vg_widget_t*)container, (float)padding);
    }
}

//=============================================================================
// Widget State Functions
//=============================================================================

int64_t rt_widget_is_hovered(void* widget) {
    if (!widget) return 0;
    return (((vg_widget_t*)widget)->state & VG_STATE_HOVERED) ? 1 : 0;
}

int64_t rt_widget_is_pressed(void* widget) {
    if (!widget) return 0;
    return (((vg_widget_t*)widget)->state & VG_STATE_PRESSED) ? 1 : 0;
}

int64_t rt_widget_is_focused(void* widget) {
    if (!widget) return 0;
    return (((vg_widget_t*)widget)->state & VG_STATE_FOCUSED) ? 1 : 0;
}

// Global for tracking last clicked widget (set by GUI.App.Poll)
static vg_widget_t* g_last_clicked_widget = NULL;

void rt_gui_set_last_clicked(void* widget) {
    g_last_clicked_widget = (vg_widget_t*)widget;
}

int64_t rt_widget_was_clicked(void* widget) {
    if (!widget) return 0;
    return (g_last_clicked_widget == widget) ? 1 : 0;
}

void rt_widget_set_position(void* widget, int64_t x, int64_t y) {
    if (widget) {
        vg_widget_t* w = (vg_widget_t*)widget;
        w->x = (float)x;
        w->y = (float)y;
    }
}

//=============================================================================
// Dropdown Widget
//=============================================================================

void* rt_dropdown_new(void* parent) {
    return vg_dropdown_create((vg_widget_t*)parent);
}

int64_t rt_dropdown_add_item(void* dropdown, rt_string text) {
    if (!dropdown) return -1;
    char* ctext = rt_string_to_cstr(text);
    int64_t index = vg_dropdown_add_item((vg_dropdown_t*)dropdown, ctext);
    free(ctext);
    return index;
}

void rt_dropdown_remove_item(void* dropdown, int64_t index) {
    if (dropdown) {
        vg_dropdown_remove_item((vg_dropdown_t*)dropdown, (int)index);
    }
}

void rt_dropdown_clear(void* dropdown) {
    if (dropdown) {
        vg_dropdown_clear((vg_dropdown_t*)dropdown);
    }
}

void rt_dropdown_set_selected(void* dropdown, int64_t index) {
    if (dropdown) {
        vg_dropdown_set_selected((vg_dropdown_t*)dropdown, (int)index);
    }
}

int64_t rt_dropdown_get_selected(void* dropdown) {
    if (!dropdown) return -1;
    return vg_dropdown_get_selected((vg_dropdown_t*)dropdown);
}

rt_string rt_dropdown_get_selected_text(void* dropdown) {
    if (!dropdown) return rt_str_empty();
    const char* text = vg_dropdown_get_selected_text((vg_dropdown_t*)dropdown);
    if (!text) return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_dropdown_set_placeholder(void* dropdown, rt_string placeholder) {
    if (!dropdown) return;
    char* ctext = rt_string_to_cstr(placeholder);
    vg_dropdown_set_placeholder((vg_dropdown_t*)dropdown, ctext);
    free(ctext);
}

//=============================================================================
// Slider Widget
//=============================================================================

void* rt_slider_new(void* parent, int64_t horizontal) {
    vg_slider_orientation_t orient = horizontal ? VG_SLIDER_HORIZONTAL : VG_SLIDER_VERTICAL;
    return vg_slider_create((vg_widget_t*)parent, orient);
}

void rt_slider_set_value(void* slider, double value) {
    if (slider) {
        vg_slider_set_value((vg_slider_t*)slider, (float)value);
    }
}

double rt_slider_get_value(void* slider) {
    if (!slider) return 0.0;
    return (double)vg_slider_get_value((vg_slider_t*)slider);
}

void rt_slider_set_range(void* slider, double min_val, double max_val) {
    if (slider) {
        vg_slider_set_range((vg_slider_t*)slider, (float)min_val, (float)max_val);
    }
}

void rt_slider_set_step(void* slider, double step) {
    if (slider) {
        vg_slider_set_step((vg_slider_t*)slider, (float)step);
    }
}

//=============================================================================
// ProgressBar Widget
//=============================================================================

void* rt_progressbar_new(void* parent) {
    return vg_progressbar_create((vg_widget_t*)parent);
}

void rt_progressbar_set_value(void* progress, double value) {
    if (progress) {
        vg_progressbar_set_value((vg_progressbar_t*)progress, (float)value);
    }
}

double rt_progressbar_get_value(void* progress) {
    if (!progress) return 0.0;
    return (double)vg_progressbar_get_value((vg_progressbar_t*)progress);
}

//=============================================================================
// ListBox Widget
//=============================================================================

void* rt_listbox_new(void* parent) {
    return vg_listbox_create((vg_widget_t*)parent);
}

void* rt_listbox_add_item(void* listbox, rt_string text) {
    if (!listbox) return NULL;
    char* ctext = rt_string_to_cstr(text);
    vg_listbox_item_t* item = vg_listbox_add_item((vg_listbox_t*)listbox, ctext, NULL);
    free(ctext);
    return item;
}

void rt_listbox_remove_item(void* listbox, void* item) {
    if (listbox && item) {
        vg_listbox_remove_item((vg_listbox_t*)listbox, (vg_listbox_item_t*)item);
    }
}

void rt_listbox_clear(void* listbox) {
    if (listbox) {
        vg_listbox_clear((vg_listbox_t*)listbox);
    }
}

void rt_listbox_select(void* listbox, void* item) {
    if (listbox) {
        vg_listbox_select((vg_listbox_t*)listbox, (vg_listbox_item_t*)item);
    }
}

void* rt_listbox_get_selected(void* listbox) {
    if (!listbox) return NULL;
    return vg_listbox_get_selected((vg_listbox_t*)listbox);
}

//=============================================================================
// RadioButton Widget
//=============================================================================

void* rt_radiogroup_new(void) {
    return vg_radiogroup_create();
}

void rt_radiogroup_destroy(void* group) {
    if (group) {
        vg_radiogroup_destroy((vg_radiogroup_t*)group);
    }
}

void* rt_radiobutton_new(void* parent, rt_string text, void* group) {
    char* ctext = rt_string_to_cstr(text);
    vg_radiobutton_t* radio = vg_radiobutton_create((vg_widget_t*)parent, ctext, (vg_radiogroup_t*)group);
    free(ctext);
    return radio;
}

int64_t rt_radiobutton_is_selected(void* radio) {
    if (!radio) return 0;
    return vg_radiobutton_is_selected((vg_radiobutton_t*)radio) ? 1 : 0;
}

void rt_radiobutton_set_selected(void* radio, int64_t selected) {
    if (radio) {
        vg_radiobutton_set_selected((vg_radiobutton_t*)radio, selected != 0);
    }
}

//=============================================================================
// Spinner Widget
//=============================================================================

void* rt_spinner_new(void* parent) {
    return vg_spinner_create((vg_widget_t*)parent);
}

void rt_spinner_set_value(void* spinner, double value) {
    if (spinner) {
        vg_spinner_set_value((vg_spinner_t*)spinner, value);
    }
}

double rt_spinner_get_value(void* spinner) {
    if (!spinner) return 0.0;
    return vg_spinner_get_value((vg_spinner_t*)spinner);
}

void rt_spinner_set_range(void* spinner, double min_val, double max_val) {
    if (spinner) {
        vg_spinner_set_range((vg_spinner_t*)spinner, min_val, max_val);
    }
}

void rt_spinner_set_step(void* spinner, double step) {
    if (spinner) {
        vg_spinner_set_step((vg_spinner_t*)spinner, step);
    }
}

void rt_spinner_set_decimals(void* spinner, int64_t decimals) {
    if (spinner) {
        vg_spinner_set_decimals((vg_spinner_t*)spinner, (int)decimals);
    }
}

//=============================================================================
// Image Widget
//=============================================================================

void* rt_image_new(void* parent) {
    return vg_image_create((vg_widget_t*)parent);
}

void rt_image_set_pixels(void* image, void* pixels, int64_t width, int64_t height) {
    if (image && pixels) {
        vg_image_set_pixels((vg_image_t*)image, (const uint8_t*)pixels, (int)width, (int)height);
    }
}

void rt_image_clear(void* image) {
    if (image) {
        vg_image_clear((vg_image_t*)image);
    }
}

void rt_image_set_scale_mode(void* image, int64_t mode) {
    if (image) {
        vg_image_set_scale_mode((vg_image_t*)image, (vg_image_scale_t)mode);
    }
}

void rt_image_set_opacity(void* image, double opacity) {
    if (image) {
        vg_image_set_opacity((vg_image_t*)image, (float)opacity);
    }
}
