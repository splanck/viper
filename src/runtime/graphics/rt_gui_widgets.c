//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_widgets.c
// Purpose: Runtime bindings for the ViperGUI base widget API and fundamental
//   widgets: font loading/destroy, widget visibility/enabled/size/flex/margin,
//   Container, Label, Button (with icon support), TextInput (with undo/redo),
//   Checkbox, RadioButton, Slider, ProgressBar, Image, ListBox, ComboBox,
//   and the tab-order focus system. This file is the foundational widget layer
//   on which all other GUI runtime files depend.
//
// Key invariants:
//   - All widget functions guard against NULL widget pointer before delegating
//     to vg_widget_* or the specific widget's vg_* API.
//   - Tab order is built lazily by vg_build_tab_order; explicit tab_index values
//     sort before default (-1) entries in DFS order.
//   - TextInput undo stack uses a "push after edit" model: the initial empty
//     string is pushed at creation; each insert/delete pushes the new state.
//   - Button icon is stored as an owned char* (icon_text) in the vg_button_t;
//     icon_pos 0 = left, 1 = right; drawn 4 px gap from the label.
//   - ListBox and ComboBox item indices are zero-based; out-of-range indices
//     return -1 / NULL from get_selected calls.
//
// Ownership/Lifetime:
//   - All widget objects are vg_widget_t* (or subtype) owned by the vg widget
//     tree; vg_widget_destroy() on any ancestor frees the full subtree.
//   - Font objects (vg_font_t*) are manually managed: load with rt_font_load,
//     free with rt_font_destroy; widget references do not extend font lifetime.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/include/vg.h (ViperGUI C API),
//        src/runtime/graphics/rt_gui_app.c (default font, s_current_app)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// Font Functions
//=============================================================================

void *rt_font_load(rt_string path)
{
    RT_ASSERT_MAIN_THREAD();
    char *cpath = rt_string_to_cstr(path);
    if (!cpath)
        return NULL;

    vg_font_t *font = vg_font_load_file(cpath);
    free(cpath);
    return font;
}

/// @brief Perform font destroy operation.
/// @param font
void rt_font_destroy(void *font)
{
    RT_ASSERT_MAIN_THREAD();
    if (font)
    {
        vg_font_destroy((vg_font_t *)font);
    }
}

//=============================================================================
// Widget Functions
//=============================================================================

/// @brief Perform widget destroy operation.
/// @param widget
void rt_widget_destroy(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (widget)
    {
        vg_widget_destroy((vg_widget_t *)widget);
    }
}

/// @brief Perform widget set visible operation.
/// @param widget
/// @param visible
void rt_widget_set_visible(void *widget, int64_t visible)
{
    RT_ASSERT_MAIN_THREAD();
    if (widget)
    {
        vg_widget_set_visible((vg_widget_t *)widget, visible != 0);
    }
}

/// @brief Perform widget set enabled operation.
/// @param widget
/// @param enabled
void rt_widget_set_enabled(void *widget, int64_t enabled)
{
    RT_ASSERT_MAIN_THREAD();
    if (widget)
    {
        vg_widget_set_enabled((vg_widget_t *)widget, enabled != 0);
    }
}

/// @brief Perform widget set size operation.
/// @param widget
/// @param width
/// @param height
void rt_widget_set_size(void *widget, int64_t width, int64_t height)
{
    RT_ASSERT_MAIN_THREAD();
    if (widget)
    {
        vg_widget_set_fixed_size((vg_widget_t *)widget, (float)width, (float)height);
    }
}

/// @brief Perform widget set flex operation.
/// @param widget
/// @param flex
void rt_widget_set_flex(void *widget, double flex)
{
    RT_ASSERT_MAIN_THREAD();
    if (widget)
    {
        vg_widget_set_flex((vg_widget_t *)widget, (float)flex);
    }
}

/// @brief Perform widget add child operation.
/// @param parent
/// @param child
void rt_widget_add_child(void *parent, void *child)
{
    RT_ASSERT_MAIN_THREAD();
    if (parent && child)
    {
        vg_widget_add_child((vg_widget_t *)parent, (vg_widget_t *)child);
    }
}

// API-005: SetMargin
/// @brief Perform widget set margin operation.
/// @param widget
/// @param margin
void rt_widget_set_margin(void *widget, int64_t margin)
{
    RT_ASSERT_MAIN_THREAD();
    if (widget)
        vg_widget_set_margin((vg_widget_t *)widget, (float)margin);
}

/// @brief Perform widget set tab index operation.
/// @param widget
/// @param idx
void rt_widget_set_tab_index(void *widget, int64_t idx)
{
    RT_ASSERT_MAIN_THREAD();
    if (widget)
        vg_widget_set_tab_index((vg_widget_t *)widget, (int)idx);
}

// BINDING-003: GuiWidget read accessors
/// @brief Perform widget is visible operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_visible(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return ((vg_widget_t *)widget)->visible ? 1 : 0;
}

/// @brief Perform widget is enabled operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_enabled(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return ((vg_widget_t *)widget)->enabled ? 1 : 0;
}

/// @brief Perform widget get width operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_width(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->width;
}

/// @brief Perform widget get height operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_height(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->height;
}

/// @brief Perform widget get x operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_x(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->x;
}

/// @brief Perform widget get y operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_y(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->y;
}

/// @brief Perform widget get flex operation.
/// @param widget
/// @return Result value.
double rt_widget_get_flex(void *widget)
{
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0.0;
    return (double)((vg_widget_t *)widget)->layout.flex;
}

//=============================================================================
// Label Widget
//=============================================================================

void *rt_label_new(void *parent, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    char *ctext = rt_string_to_cstr(text);
    vg_label_t *label = vg_label_create((vg_widget_t *)parent, ctext ? ctext : "");
    free(ctext);
    return label;
}

/// @brief Perform label set text operation.
/// @param label
/// @param text
void rt_label_set_text(void *label, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!label)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_label_set_text((vg_label_t *)label, ctext);
    free(ctext);
}

/// @brief Perform label set font operation.
/// @param label
/// @param font
/// @param size
void rt_label_set_font(void *label, void *font, double size)
{
    RT_ASSERT_MAIN_THREAD();
    if (label)
    {
        vg_label_set_font((vg_label_t *)label, (vg_font_t *)font, (float)size);
    }
}

/// @brief Perform label set color operation.
/// @param label
/// @param color
void rt_label_set_color(void *label, int64_t color)
{
    RT_ASSERT_MAIN_THREAD();
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
    RT_ASSERT_MAIN_THREAD();
    char *ctext = rt_string_to_cstr(text);
    vg_button_t *button = vg_button_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return button;
}

/// @brief Perform button set text operation.
/// @param button
/// @param text
void rt_button_set_text(void *button, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!button)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_button_set_text((vg_button_t *)button, ctext);
    free(ctext);
}

/// @brief Perform button set font operation.
/// @param button
/// @param font
/// @param size
void rt_button_set_font(void *button, void *font, double size)
{
    RT_ASSERT_MAIN_THREAD();
    if (button)
    {
        vg_button_set_font((vg_button_t *)button, (vg_font_t *)font, (float)size);
    }
}

/// @brief Perform button set style operation.
/// @param button
/// @param style
void rt_button_set_style(void *button, int64_t style)
{
    RT_ASSERT_MAIN_THREAD();
    if (button)
    {
        vg_button_set_style((vg_button_t *)button, (vg_button_style_t)style);
    }
}

/// @brief Perform button set icon operation.
/// @param button
/// @param icon
void rt_button_set_icon(void *button, rt_string icon)
{
    RT_ASSERT_MAIN_THREAD();
    if (!button)
        return;
    char *cicon = rt_string_to_cstr(icon);
    vg_button_set_icon((vg_button_t *)button, cicon);
    free(cicon);
}

/// @brief Perform button set icon pos operation.
/// @param button
/// @param pos
void rt_button_set_icon_pos(void *button, int64_t pos)
{
    RT_ASSERT_MAIN_THREAD();
    if (button)
        vg_button_set_icon_position((vg_button_t *)button, (int)pos);
}

//=============================================================================
// TextInput Widget
//=============================================================================

void *rt_textinput_new(void *parent)
{
    RT_ASSERT_MAIN_THREAD();
    return vg_textinput_create((vg_widget_t *)parent);
}

/// @brief Perform textinput set text operation.
/// @param input
/// @param text
void rt_textinput_set_text(void *input, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_textinput_set_text((vg_textinput_t *)input, ctext);
    free(ctext);
}

/// @brief Perform textinput get text operation.
/// @param input
/// @return Result value.
rt_string rt_textinput_get_text(void *input)
{
    RT_ASSERT_MAIN_THREAD();
    if (!input)
        return rt_str_empty();
    const char *text = vg_textinput_get_text((vg_textinput_t *)input);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

/// @brief Perform textinput set placeholder operation.
/// @param input
/// @param placeholder
void rt_textinput_set_placeholder(void *input, rt_string placeholder)
{
    RT_ASSERT_MAIN_THREAD();
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_textinput_set_placeholder((vg_textinput_t *)input, ctext);
    free(ctext);
}

/// @brief Perform textinput set font operation.
/// @param input
/// @param font
/// @param size
void rt_textinput_set_font(void *input, void *font, double size)
{
    RT_ASSERT_MAIN_THREAD();
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
    RT_ASSERT_MAIN_THREAD();
    char *ctext = rt_string_to_cstr(text);
    vg_checkbox_t *checkbox = vg_checkbox_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return checkbox;
}

/// @brief Check checkbox set checked.
/// @param checkbox
/// @param checked
void rt_checkbox_set_checked(void *checkbox, int64_t checked)
{
    RT_ASSERT_MAIN_THREAD();
    if (checkbox)
    {
        vg_checkbox_set_checked((vg_checkbox_t *)checkbox, checked != 0);
    }
}

/// @brief Check checkbox is checked.
/// @param checkbox
/// @return Result value.
int64_t rt_checkbox_is_checked(void *checkbox)
{
    RT_ASSERT_MAIN_THREAD();
    if (!checkbox)
        return 0;
    return vg_checkbox_is_checked((vg_checkbox_t *)checkbox) ? 1 : 0;
}

/// @brief Check checkbox set text.
/// @param checkbox
/// @param text
void rt_checkbox_set_text(void *checkbox, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
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
    RT_ASSERT_MAIN_THREAD();
    return vg_scrollview_create((vg_widget_t *)parent);
}

/// @brief Perform scrollview set scroll operation.
/// @param scroll
/// @param x
/// @param y
void rt_scrollview_set_scroll(void *scroll, double x, double y)
{
    RT_ASSERT_MAIN_THREAD();
    if (scroll)
    {
        vg_scrollview_set_scroll((vg_scrollview_t *)scroll, (float)x, (float)y);
    }
}

/// @brief Perform scrollview set content size operation.
/// @param scroll
/// @param width
/// @param height
void rt_scrollview_set_content_size(void *scroll, double width, double height)
{
    RT_ASSERT_MAIN_THREAD();
    if (scroll)
    {
        vg_scrollview_set_content_size((vg_scrollview_t *)scroll, (float)width, (float)height);
    }
}

// BINDING-004: ScrollView scroll position query
/// @brief Perform scrollview get scroll x operation.
/// @param scroll
/// @return Result value.
double rt_scrollview_get_scroll_x(void *scroll)
{
    RT_ASSERT_MAIN_THREAD();
    if (!scroll)
        return 0.0;
    float x = 0.0f, y = 0.0f;
    vg_scrollview_get_scroll((vg_scrollview_t *)scroll, &x, &y);
    return (double)x;
}

/// @brief Perform scrollview get scroll y operation.
/// @param scroll
/// @return Result value.
double rt_scrollview_get_scroll_y(void *scroll)
{
    RT_ASSERT_MAIN_THREAD();
    if (!scroll)
        return 0.0;
    float x = 0.0f, y = 0.0f;
    vg_scrollview_get_scroll((vg_scrollview_t *)scroll, &x, &y);
    return (double)y;
}

//=============================================================================
// TreeView Widget
//=============================================================================

void *rt_treeview_new(void *parent)
{
    RT_ASSERT_MAIN_THREAD();
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
    RT_ASSERT_MAIN_THREAD();
    if (!tree)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_tree_node_t *node =
        vg_treeview_add_node((vg_treeview_t *)tree, (vg_tree_node_t *)parent_node, ctext);
    free(ctext);
    return node;
}

/// @brief Perform treeview remove node operation.
/// @param tree
/// @param node
void rt_treeview_remove_node(void *tree, void *node)
{
    RT_ASSERT_MAIN_THREAD();
    if (tree && node)
    {
        vg_treeview_remove_node((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Perform treeview clear operation.
/// @param tree
void rt_treeview_clear(void *tree)
{
    RT_ASSERT_MAIN_THREAD();
    if (tree)
    {
        vg_treeview_clear((vg_treeview_t *)tree);
    }
}

/// @brief Perform treeview expand operation.
/// @param tree
/// @param node
void rt_treeview_expand(void *tree, void *node)
{
    RT_ASSERT_MAIN_THREAD();
    if (tree && node)
    {
        vg_treeview_expand((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Perform treeview collapse operation.
/// @param tree
/// @param node
void rt_treeview_collapse(void *tree, void *node)
{
    RT_ASSERT_MAIN_THREAD();
    if (tree && node)
    {
        vg_treeview_collapse((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Perform treeview select operation.
/// @param tree
/// @param node
void rt_treeview_select(void *tree, void *node)
{
    RT_ASSERT_MAIN_THREAD();
    if (tree)
    {
        vg_treeview_select((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Perform treeview set font operation.
/// @param tree
/// @param font
/// @param size
void rt_treeview_set_font(void *tree, void *font, double size)
{
    RT_ASSERT_MAIN_THREAD();
    if (tree)
    {
        vg_treeview_set_font((vg_treeview_t *)tree, (vg_font_t *)font, (float)size);
    }
}

void *rt_treeview_get_selected(void *tree)
{
    RT_ASSERT_MAIN_THREAD();
    if (!tree)
        return NULL;
    vg_treeview_t *tv = (vg_treeview_t *)tree;
    return tv->selected;
}

/// @brief Perform treeview was selection changed operation.
/// @param tree
/// @return Result value.
int64_t rt_treeview_was_selection_changed(void *tree)
{
    RT_ASSERT_MAIN_THREAD();
    if (!tree)
        return 0;
    vg_treeview_t *tv = (vg_treeview_t *)tree;

    // Per-instance selection tracking using prev_selected field
    // (matches the pattern used by rt_tabbar_was_changed / prev_active_tab).
    if (tv->selected != tv->prev_selected)
    {
        tv->prev_selected = tv->selected;
        return 1;
    }
    return 0;
}

/// @brief Perform treeview node get text operation.
/// @param node
/// @return Result value.
rt_string rt_treeview_node_get_text(void *node)
{
    RT_ASSERT_MAIN_THREAD();
    if (!node)
        return rt_str_empty();
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    if (!n->text)
        return rt_str_empty();
    return rt_string_from_bytes(n->text, strlen(n->text));
}

/// @brief Perform treeview node set data operation.
/// @param node
/// @param data
void rt_treeview_node_set_data(void *node, rt_string data)
{
    RT_ASSERT_MAIN_THREAD();
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

/// @brief Perform treeview node get data operation.
/// @param node
/// @return Result value.
rt_string rt_treeview_node_get_data(void *node)
{
    RT_ASSERT_MAIN_THREAD();
    if (!node)
        return rt_str_empty();
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    if (!n->user_data)
        return rt_str_empty();
    const char *data = (const char *)n->user_data;
    return rt_string_from_bytes(data, strlen(data));
}

/// @brief Perform treeview node is expanded operation.
/// @param node
/// @return Result value.
int64_t rt_treeview_node_is_expanded(void *node)
{
    RT_ASSERT_MAIN_THREAD();
    if (!node)
        return 0;
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    return n->expanded ? 1 : 0;
}

#else /* !VIPER_ENABLE_GRAPHICS */

void *rt_font_load(rt_string path)
{
    (void)path;
    return NULL;
}

/// @brief Perform font destroy operation.
/// @param font
void rt_font_destroy(void *font)
{
    (void)font;
}

/// @brief Perform widget destroy operation.
/// @param widget
void rt_widget_destroy(void *widget)
{
    (void)widget;
}

/// @brief Perform widget set visible operation.
/// @param widget
/// @param visible
void rt_widget_set_visible(void *widget, int64_t visible)
{
    (void)widget;
    (void)visible;
}

/// @brief Perform widget set enabled operation.
/// @param widget
/// @param enabled
void rt_widget_set_enabled(void *widget, int64_t enabled)
{
    (void)widget;
    (void)enabled;
}

/// @brief Perform widget set size operation.
/// @param widget
/// @param width
/// @param height
void rt_widget_set_size(void *widget, int64_t width, int64_t height)
{
    (void)widget;
    (void)width;
    (void)height;
}

/// @brief Perform widget set flex operation.
/// @param widget
/// @param flex
void rt_widget_set_flex(void *widget, double flex)
{
    (void)widget;
    (void)flex;
}

/// @brief Perform widget add child operation.
/// @param parent
/// @param child
void rt_widget_add_child(void *parent, void *child)
{
    (void)parent;
    (void)child;
}

/// @brief Perform widget set margin operation.
/// @param widget
/// @param margin
void rt_widget_set_margin(void *widget, int64_t margin)
{
    (void)widget;
    (void)margin;
}

/// @brief Perform widget set tab index operation.
/// @param widget
/// @param idx
void rt_widget_set_tab_index(void *widget, int64_t idx)
{
    (void)widget;
    (void)idx;
}

/// @brief Perform widget is visible operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_visible(void *widget)
{
    (void)widget;
    return 0;
}

/// @brief Perform widget is enabled operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_enabled(void *widget)
{
    (void)widget;
    return 0;
}

/// @brief Perform widget get width operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_width(void *widget)
{
    (void)widget;
    return 0;
}

/// @brief Perform widget get height operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_height(void *widget)
{
    (void)widget;
    return 0;
}

/// @brief Perform widget get x operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_x(void *widget)
{
    (void)widget;
    return 0;
}

/// @brief Perform widget get y operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_get_y(void *widget)
{
    (void)widget;
    return 0;
}

/// @brief Perform widget get flex operation.
/// @param widget
/// @return Result value.
double rt_widget_get_flex(void *widget)
{
    (void)widget;
    return 0.0;
}

void *rt_label_new(void *parent, rt_string text)
{
    (void)parent;
    (void)text;
    return NULL;
}

/// @brief Perform label set text operation.
/// @param label
/// @param text
void rt_label_set_text(void *label, rt_string text)
{
    (void)label;
    (void)text;
}

/// @brief Perform label set font operation.
/// @param label
/// @param font
/// @param size
void rt_label_set_font(void *label, void *font, double size)
{
    (void)label;
    (void)font;
    (void)size;
}

/// @brief Perform label set color operation.
/// @param label
/// @param color
void rt_label_set_color(void *label, int64_t color)
{
    (void)label;
    (void)color;
}

void *rt_button_new(void *parent, rt_string text)
{
    (void)parent;
    (void)text;
    return NULL;
}

/// @brief Perform button set text operation.
/// @param button
/// @param text
void rt_button_set_text(void *button, rt_string text)
{
    (void)button;
    (void)text;
}

/// @brief Perform button set font operation.
/// @param button
/// @param font
/// @param size
void rt_button_set_font(void *button, void *font, double size)
{
    (void)button;
    (void)font;
    (void)size;
}

/// @brief Perform button set style operation.
/// @param button
/// @param style
void rt_button_set_style(void *button, int64_t style)
{
    (void)button;
    (void)style;
}

/// @brief Perform button set icon operation.
/// @param button
/// @param icon
void rt_button_set_icon(void *button, rt_string icon)
{
    (void)button;
    (void)icon;
}

/// @brief Perform button set icon pos operation.
/// @param button
/// @param pos
void rt_button_set_icon_pos(void *button, int64_t pos)
{
    (void)button;
    (void)pos;
}

void *rt_textinput_new(void *parent)
{
    (void)parent;
    return NULL;
}

/// @brief Perform textinput set text operation.
/// @param input
/// @param text
void rt_textinput_set_text(void *input, rt_string text)
{
    (void)input;
    (void)text;
}

/// @brief Perform textinput get text operation.
/// @param input
/// @return Result value.
rt_string rt_textinput_get_text(void *input)
{
    (void)input;
    return rt_str_empty();
}

/// @brief Perform textinput set placeholder operation.
/// @param input
/// @param placeholder
void rt_textinput_set_placeholder(void *input, rt_string placeholder)
{
    (void)input;
    (void)placeholder;
}

/// @brief Perform textinput set font operation.
/// @param input
/// @param font
/// @param size
void rt_textinput_set_font(void *input, void *font, double size)
{
    (void)input;
    (void)font;
    (void)size;
}

void *rt_checkbox_new(void *parent, rt_string text)
{
    (void)parent;
    (void)text;
    return NULL;
}

/// @brief Check checkbox set checked.
/// @param checkbox
/// @param checked
void rt_checkbox_set_checked(void *checkbox, int64_t checked)
{
    (void)checkbox;
    (void)checked;
}

/// @brief Check checkbox is checked.
/// @param checkbox
/// @return Result value.
int64_t rt_checkbox_is_checked(void *checkbox)
{
    (void)checkbox;
    return 0;
}

/// @brief Check checkbox set text.
/// @param checkbox
/// @param text
void rt_checkbox_set_text(void *checkbox, rt_string text)
{
    (void)checkbox;
    (void)text;
}

void *rt_scrollview_new(void *parent)
{
    (void)parent;
    return NULL;
}

/// @brief Perform scrollview set scroll operation.
/// @param scroll
/// @param x
/// @param y
void rt_scrollview_set_scroll(void *scroll, double x, double y)
{
    (void)scroll;
    (void)x;
    (void)y;
}

/// @brief Perform scrollview set content size operation.
/// @param scroll
/// @param width
/// @param height
void rt_scrollview_set_content_size(void *scroll, double width, double height)
{
    (void)scroll;
    (void)width;
    (void)height;
}

/// @brief Perform scrollview get scroll x operation.
/// @param scroll
/// @return Result value.
double rt_scrollview_get_scroll_x(void *scroll)
{
    (void)scroll;
    return 0.0;
}

/// @brief Perform scrollview get scroll y operation.
/// @param scroll
/// @return Result value.
double rt_scrollview_get_scroll_y(void *scroll)
{
    (void)scroll;
    return 0.0;
}

void *rt_treeview_new(void *parent)
{
    (void)parent;
    return NULL;
}

void *rt_treeview_add_node(void *tree, void *parent_node, rt_string text)
{
    (void)tree;
    (void)parent_node;
    (void)text;
    return NULL;
}

/// @brief Perform treeview remove node operation.
/// @param tree
/// @param node
void rt_treeview_remove_node(void *tree, void *node)
{
    (void)tree;
    (void)node;
}

/// @brief Perform treeview clear operation.
/// @param tree
void rt_treeview_clear(void *tree)
{
    (void)tree;
}

/// @brief Perform treeview expand operation.
/// @param tree
/// @param node
void rt_treeview_expand(void *tree, void *node)
{
    (void)tree;
    (void)node;
}

/// @brief Perform treeview collapse operation.
/// @param tree
/// @param node
void rt_treeview_collapse(void *tree, void *node)
{
    (void)tree;
    (void)node;
}

/// @brief Perform treeview select operation.
/// @param tree
/// @param node
void rt_treeview_select(void *tree, void *node)
{
    (void)tree;
    (void)node;
}

/// @brief Perform treeview set font operation.
/// @param tree
/// @param font
/// @param size
void rt_treeview_set_font(void *tree, void *font, double size)
{
    (void)tree;
    (void)font;
    (void)size;
}

void *rt_treeview_get_selected(void *tree)
{
    (void)tree;
    return NULL;
}

/// @brief Perform treeview was selection changed operation.
/// @param tree
/// @return Result value.
int64_t rt_treeview_was_selection_changed(void *tree)
{
    (void)tree;
    return 0;
}

/// @brief Perform treeview node get text operation.
/// @param node
/// @return Result value.
rt_string rt_treeview_node_get_text(void *node)
{
    (void)node;
    return rt_str_empty();
}

/// @brief Perform treeview node set data operation.
/// @param node
/// @param data
void rt_treeview_node_set_data(void *node, rt_string data)
{
    (void)node;
    (void)data;
}

/// @brief Perform treeview node get data operation.
/// @param node
/// @return Result value.
rt_string rt_treeview_node_get_data(void *node)
{
    (void)node;
    return rt_str_empty();
}

/// @brief Perform treeview node is expanded operation.
/// @param node
/// @return Result value.
int64_t rt_treeview_node_is_expanded(void *node)
{
    (void)node;
    return 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
