//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_gui_widgets.c
// Purpose: Font, base widget, and basic widget implementations.
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"

//=============================================================================
// Font Functions
//=============================================================================

void *rt_font_load(rt_string path)
{
    char *cpath = rt_string_to_cstr(path);
    if (!cpath)
        return NULL;

    vg_font_t *font = vg_font_load_file(cpath);
    free(cpath);
    return font;
}

void rt_font_destroy(void *font)
{
    if (font)
    {
        vg_font_destroy((vg_font_t *)font);
    }
}

//=============================================================================
// Widget Functions
//=============================================================================

void rt_widget_destroy(void *widget)
{
    if (widget)
    {
        vg_widget_destroy((vg_widget_t *)widget);
    }
}

void rt_widget_set_visible(void *widget, int64_t visible)
{
    if (widget)
    {
        vg_widget_set_visible((vg_widget_t *)widget, visible != 0);
    }
}

void rt_widget_set_enabled(void *widget, int64_t enabled)
{
    if (widget)
    {
        vg_widget_set_enabled((vg_widget_t *)widget, enabled != 0);
    }
}

void rt_widget_set_size(void *widget, int64_t width, int64_t height)
{
    if (widget)
    {
        vg_widget_set_fixed_size((vg_widget_t *)widget, (float)width, (float)height);
    }
}

void rt_widget_set_flex(void *widget, double flex)
{
    if (widget)
    {
        vg_widget_set_flex((vg_widget_t *)widget, (float)flex);
    }
}

void rt_widget_add_child(void *parent, void *child)
{
    if (parent && child)
    {
        vg_widget_add_child((vg_widget_t *)parent, (vg_widget_t *)child);
    }
}

//=============================================================================
// Label Widget
//=============================================================================

void *rt_label_new(void *parent, rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    vg_label_t *label = vg_label_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return label;
}

void rt_label_set_text(void *label, rt_string text)
{
    if (!label)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_label_set_text((vg_label_t *)label, ctext);
    free(ctext);
}

void rt_label_set_font(void *label, void *font, double size)
{
    if (label)
    {
        vg_label_set_font((vg_label_t *)label, (vg_font_t *)font, (float)size);
    }
}

void rt_label_set_color(void *label, int64_t color)
{
    if (label)
    {
        vg_label_set_color((vg_label_t *)label, (uint32_t)color);
    }
}

//=============================================================================
// Button Widget
//=============================================================================

void *rt_button_new(void *parent, rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    vg_button_t *button = vg_button_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return button;
}

void rt_button_set_text(void *button, rt_string text)
{
    if (!button)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_button_set_text((vg_button_t *)button, ctext);
    free(ctext);
}

void rt_button_set_font(void *button, void *font, double size)
{
    if (button)
    {
        vg_button_set_font((vg_button_t *)button, (vg_font_t *)font, (float)size);
    }
}

void rt_button_set_style(void *button, int64_t style)
{
    if (button)
    {
        vg_button_set_style((vg_button_t *)button, (vg_button_style_t)style);
    }
}

//=============================================================================
// TextInput Widget
//=============================================================================

void *rt_textinput_new(void *parent)
{
    return vg_textinput_create((vg_widget_t *)parent);
}

void rt_textinput_set_text(void *input, rt_string text)
{
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_textinput_set_text((vg_textinput_t *)input, ctext);
    free(ctext);
}

rt_string rt_textinput_get_text(void *input)
{
    if (!input)
        return rt_str_empty();
    const char *text = vg_textinput_get_text((vg_textinput_t *)input);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_textinput_set_placeholder(void *input, rt_string placeholder)
{
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_textinput_set_placeholder((vg_textinput_t *)input, ctext);
    free(ctext);
}

void rt_textinput_set_font(void *input, void *font, double size)
{
    if (input)
    {
        vg_textinput_set_font((vg_textinput_t *)input, (vg_font_t *)font, (float)size);
    }
}

//=============================================================================
// Checkbox Widget
//=============================================================================

void *rt_checkbox_new(void *parent, rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    vg_checkbox_t *checkbox = vg_checkbox_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return checkbox;
}

void rt_checkbox_set_checked(void *checkbox, int64_t checked)
{
    if (checkbox)
    {
        vg_checkbox_set_checked((vg_checkbox_t *)checkbox, checked != 0);
    }
}

int64_t rt_checkbox_is_checked(void *checkbox)
{
    if (!checkbox)
        return 0;
    return vg_checkbox_is_checked((vg_checkbox_t *)checkbox) ? 1 : 0;
}

void rt_checkbox_set_text(void *checkbox, rt_string text)
{
    if (!checkbox)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_checkbox_set_text((vg_checkbox_t *)checkbox, ctext);
    free(ctext);
}

//=============================================================================
// ScrollView Widget
//=============================================================================

void *rt_scrollview_new(void *parent)
{
    return vg_scrollview_create((vg_widget_t *)parent);
}

void rt_scrollview_set_scroll(void *scroll, double x, double y)
{
    if (scroll)
    {
        vg_scrollview_set_scroll((vg_scrollview_t *)scroll, (float)x, (float)y);
    }
}

void rt_scrollview_set_content_size(void *scroll, double width, double height)
{
    if (scroll)
    {
        vg_scrollview_set_content_size((vg_scrollview_t *)scroll, (float)width, (float)height);
    }
}

//=============================================================================
// TreeView Widget
//=============================================================================

void *rt_treeview_new(void *parent)
{
    vg_treeview_t *tv = vg_treeview_create((vg_widget_t *)parent);
    if (tv)
    {
        rt_gui_ensure_default_font();
        if (s_current_app && s_current_app->default_font)
            vg_treeview_set_font(tv, s_current_app->default_font, s_current_app->default_font_size);
    }
    return tv;
}

void *rt_treeview_add_node(void *tree, void *parent_node, rt_string text)
{
    if (!tree)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_tree_node_t *node =
        vg_treeview_add_node((vg_treeview_t *)tree, (vg_tree_node_t *)parent_node, ctext);
    free(ctext);
    return node;
}

void rt_treeview_remove_node(void *tree, void *node)
{
    if (tree && node)
    {
        vg_treeview_remove_node((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_clear(void *tree)
{
    if (tree)
    {
        vg_treeview_clear((vg_treeview_t *)tree);
    }
}

void rt_treeview_expand(void *tree, void *node)
{
    if (tree && node)
    {
        vg_treeview_expand((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_collapse(void *tree, void *node)
{
    if (tree && node)
    {
        vg_treeview_collapse((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_select(void *tree, void *node)
{
    if (tree)
    {
        vg_treeview_select((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_set_font(void *tree, void *font, double size)
{
    if (tree)
    {
        vg_treeview_set_font((vg_treeview_t *)tree, (vg_font_t *)font, (float)size);
    }
}

void *rt_treeview_get_selected(void *tree)
{
    if (!tree)
        return NULL;
    vg_treeview_t *tv = (vg_treeview_t *)tree;
    return tv->selected;
}

// Track selection changes for polling pattern
static vg_tree_node_t *g_last_treeview_selected = NULL;
static vg_treeview_t *g_last_treeview_checked = NULL;

int64_t rt_treeview_was_selection_changed(void *tree)
{
    if (!tree)
        return 0;
    vg_treeview_t *tv = (vg_treeview_t *)tree;

    // Reset tracking if checking a different tree
    if (g_last_treeview_checked != tv)
    {
        g_last_treeview_checked = tv;
        g_last_treeview_selected = tv->selected;
        return 0;
    }

    if (tv->selected != g_last_treeview_selected)
    {
        g_last_treeview_selected = tv->selected;
        return 1;
    }
    return 0;
}

rt_string rt_treeview_node_get_text(void *node)
{
    if (!node)
        return rt_str_empty();
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    if (!n->text)
        return rt_str_empty();
    return rt_string_from_bytes(n->text, strlen(n->text));
}

void rt_treeview_node_set_data(void *node, rt_string data)
{
    if (!node)
        return;
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    // Free old data if it exists
    if (n->user_data)
        free(n->user_data);
    // Store a copy of the string as user_data
    const char *cstr = rt_string_cstr(data);
    n->user_data = cstr ? strdup(cstr) : NULL;
}

rt_string rt_treeview_node_get_data(void *node)
{
    if (!node)
        return rt_str_empty();
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    if (!n->user_data)
        return rt_str_empty();
    const char *data = (const char *)n->user_data;
    return rt_string_from_bytes(data, strlen(data));
}

int64_t rt_treeview_node_is_expanded(void *node)
{
    if (!node)
        return 0;
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    return n->expanded ? 1 : 0;
}


