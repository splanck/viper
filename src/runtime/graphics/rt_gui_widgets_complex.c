//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_widgets_complex.c
// Purpose: Runtime bindings for composite ViperGUI widgets: TabBar (tab strip
//   with optional close buttons), SplitPane (resizable two-panel divider),
//   TreeView (hierarchical node list), ScrollView (scrollable container),
//   FloatingPanel (overlay panel drawn above all content), and CodeEditor (full
//   source-editor widget with syntax highlighting, gutters, and selection).
//   Each widget wraps the corresponding vg_* C widget with a Zia-callable API.
//
// Key invariants:
//   - TabBar active-tab is tracked by the vg_tabbar_t; rt_tabbar_get_active()
//     returns the raw vg_tab_t* pointer — callers must not free it.
//   - SplitPane position is a float in [0,1] representing the divider fraction;
//     clamped by the vg layout engine to [min_pos, max_pos].
//   - TreeView nodes form a pointer-linked tree; removing a node frees its
//     subtree recursively via vg_treeview_remove_node.
//   - ScrollView scroll offsets are clamped to [0, content_size - viewport_size]
//     by the vg layout engine; GetScrollX/Y may return 0 if content fits.
//   - FloatingPanel children are in a private array (not the widget tree);
//     they are drawn in paint_overlay to appear above all other content.
//   - CodeEditor selection retrieval allocates a C string that the caller owns.
//
// Ownership/Lifetime:
//   - All widget objects are vg_widget_t* (or subtype) owned by the vg widget
//     tree; vg_widget_destroy() on the root frees the entire subtree.
//   - Tab objects (vg_tab_t*) are owned by the TabBar; do not free them
//     independently.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/include/vg.h (ViperGUI C API),
//        src/runtime/graphics/rt_gui_codeeditor.c (CodeEditor enhancements)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"

//=============================================================================
// TabBar Widget
//=============================================================================

void *rt_tabbar_new(void *parent)
{
    vg_tabbar_t *tabbar = vg_tabbar_create((vg_widget_t *)parent);
    if (tabbar && s_current_app && s_current_app->default_font)
    {
        rt_gui_ensure_default_font();
        vg_tabbar_set_font(tabbar, s_current_app->default_font, s_current_app->default_font_size);
    }
    return tabbar;
}

void *rt_tabbar_add_tab(void *tabbar, rt_string title, int64_t closable)
{
    if (!tabbar)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_t *tab = vg_tabbar_add_tab((vg_tabbar_t *)tabbar, ctitle, closable != 0);
    free(ctitle);
    return tab;
}

void rt_tabbar_remove_tab(void *tabbar, void *tab)
{
    if (tabbar && tab)
    {
        vg_tabbar_remove_tab((vg_tabbar_t *)tabbar, (vg_tab_t *)tab);
    }
}

void rt_tabbar_set_active(void *tabbar, void *tab)
{
    if (tabbar)
    {
        vg_tabbar_set_active((vg_tabbar_t *)tabbar, (vg_tab_t *)tab);
    }
}

void rt_tab_set_title(void *tab, rt_string title)
{
    if (!tab)
        return;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_set_title((vg_tab_t *)tab, ctitle);
    free(ctitle);
}

void rt_tab_set_modified(void *tab, int64_t modified)
{
    if (tab)
    {
        vg_tab_set_modified((vg_tab_t *)tab, modified != 0);
    }
}

void *rt_tabbar_get_active(void *tabbar)
{
    return tabbar ? ((vg_tabbar_t *)tabbar)->active_tab : NULL;
}

int64_t rt_tabbar_get_active_index(void *tabbar)
{
    if (!tabbar)
        return -1;
    vg_tabbar_t *tb = (vg_tabbar_t *)tabbar;
    return vg_tabbar_get_tab_index(tb, tb->active_tab);
}

int64_t rt_tabbar_was_changed(void *tabbar)
{
    if (!tabbar)
        return 0;
    vg_tabbar_t *tb = (vg_tabbar_t *)tabbar;
    if (tb->active_tab != tb->prev_active_tab)
    {
        tb->prev_active_tab = tb->active_tab;
        return 1;
    }
    return 0;
}

int64_t rt_tabbar_get_tab_count(void *tabbar)
{
    return tabbar ? ((vg_tabbar_t *)tabbar)->tab_count : 0;
}

int64_t rt_tabbar_was_close_clicked(void *tabbar)
{
    if (!tabbar)
        return 0;
    return ((vg_tabbar_t *)tabbar)->close_clicked_tab ? 1 : 0;
}

int64_t rt_tabbar_get_close_clicked_index(void *tabbar)
{
    if (!tabbar)
        return -1;
    vg_tabbar_t *tb = (vg_tabbar_t *)tabbar;
    if (!tb->close_clicked_tab)
        return -1;
    int index = vg_tabbar_get_tab_index(tb, tb->close_clicked_tab);
    tb->close_clicked_tab = NULL;
    return index;
}

void *rt_tabbar_get_tab_at(void *tabbar, int64_t index)
{
    if (!tabbar)
        return NULL;
    return vg_tabbar_get_tab_at((vg_tabbar_t *)tabbar, (int)index);
}

void rt_tabbar_set_auto_close(void *tabbar, int64_t auto_close)
{
    if (tabbar)
    {
        ((vg_tabbar_t *)tabbar)->auto_close = auto_close != 0;
    }
}

//=============================================================================
// SplitPane Widget
//=============================================================================

void *rt_splitpane_new(void *parent, int64_t horizontal)
{
    vg_split_direction_t direction = horizontal ? VG_SPLIT_HORIZONTAL : VG_SPLIT_VERTICAL;
    return vg_splitpane_create((vg_widget_t *)parent, direction);
}

void rt_splitpane_set_position(void *split, double position)
{
    if (split)
    {
        vg_splitpane_set_position((vg_splitpane_t *)split, (float)position);
    }
}

// BINDING-006: SplitPane position query
double rt_splitpane_get_position(void *split)
{
    if (!split)
        return 0.5;
    return (double)vg_splitpane_get_position((vg_splitpane_t *)split);
}

void *rt_splitpane_get_first(void *split)
{
    if (!split)
        return NULL;
    return vg_splitpane_get_first((vg_splitpane_t *)split);
}

void *rt_splitpane_get_second(void *split)
{
    if (!split)
        return NULL;
    return vg_splitpane_get_second((vg_splitpane_t *)split);
}

//=============================================================================
// CodeEditor Widget
//=============================================================================

void *rt_codeeditor_new(void *parent)
{
    vg_codeeditor_t *editor = vg_codeeditor_create((vg_widget_t *)parent);
    if (editor && s_current_app)
    {
        rt_gui_ensure_default_font();
        if (s_current_app->default_font)
        {
            vg_codeeditor_set_font(
                editor, s_current_app->default_font, s_current_app->default_font_size);
        }
    }
    return editor;
}

void rt_codeeditor_set_text(void *editor, rt_string text)
{
    if (!editor)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_codeeditor_set_text((vg_codeeditor_t *)editor, ctext);
    free(ctext);
}

rt_string rt_codeeditor_get_text(void *editor)
{
    if (!editor)
        return rt_str_empty();
    char *text = vg_codeeditor_get_text((vg_codeeditor_t *)editor);
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

rt_string rt_codeeditor_get_selected_text(void *editor)
{
    if (!editor)
        return rt_str_empty();
    char *text = vg_codeeditor_get_selection((vg_codeeditor_t *)editor);
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

void rt_codeeditor_set_cursor(void *editor, int64_t line, int64_t col)
{
    if (editor)
    {
        vg_codeeditor_set_cursor((vg_codeeditor_t *)editor, (int)line, (int)col);
    }
}

void rt_codeeditor_scroll_to_line(void *editor, int64_t line)
{
    if (editor)
    {
        vg_codeeditor_scroll_to_line((vg_codeeditor_t *)editor, (int)line);
    }
}

int64_t rt_codeeditor_get_line_count(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_get_line_count((vg_codeeditor_t *)editor);
}

int64_t rt_codeeditor_is_modified(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_is_modified((vg_codeeditor_t *)editor) ? 1 : 0;
}

void rt_codeeditor_clear_modified(void *editor)
{
    if (editor)
    {
        vg_codeeditor_clear_modified((vg_codeeditor_t *)editor);
    }
}

void rt_codeeditor_set_font(void *editor, void *font, double size)
{
    if (editor)
    {
        vg_codeeditor_set_font((vg_codeeditor_t *)editor, (vg_font_t *)font, (float)size);
    }
}

double rt_codeeditor_get_font_size(void *editor)
{
    if (!editor)
        return 14.0;
    vg_codeeditor_t *ed = (vg_codeeditor_t *)editor;
    // Return logical pt size — divide stored physical pixels by HiDPI scale.
    float _s = (s_current_app && s_current_app->window)
                   ? vgfx_window_get_scale(s_current_app->window)
                   : 1.0f;
    if (_s <= 0.0f)
        _s = 1.0f;
    return (double)(ed->font_size / _s);
}

void rt_codeeditor_set_font_size(void *editor, double size)
{
    if (!editor)
        return;
    vg_codeeditor_t *ed = (vg_codeeditor_t *)editor;
    if (size > 0.0)
    {
        // Store physical pixels — multiply logical pt size by HiDPI scale.
        float _s = (s_current_app && s_current_app->window)
                       ? vgfx_window_get_scale(s_current_app->window)
                       : 1.0f;
        if (_s <= 0.0f)
            _s = 1.0f;
        ed->font_size = (float)size * _s;
        ed->line_height = ed->font_size * 1.4f; /* keep line spacing proportional to font */
        ((vg_widget_t *)ed)->needs_paint = true;
    }
}

//=============================================================================
// Theme Functions
//=============================================================================

static int s_theme_is_dark = 1; ///< Tracks current theme; 1 = dark, 0 = light.

void rt_theme_set_dark(void)
{
    s_theme_is_dark = 1;
    vg_theme_set_current(vg_theme_dark());
}

void rt_theme_set_light(void)
{
    s_theme_is_dark = 0;
    vg_theme_set_current(vg_theme_light());
}

rt_string rt_theme_get_name(void)
{
    const char *name = s_theme_is_dark ? "dark" : "light";
    return rt_string_from_bytes(name, strlen(name));
}

//=============================================================================
// Layout Functions
//=============================================================================

void *rt_vbox_new(void)
{
    return vg_vbox_create(0.0f);
}

void *rt_hbox_new(void)
{
    return vg_hbox_create(0.0f);
}

void rt_container_set_spacing(void *container, double spacing)
{
    if (!container)
        return;
    // Both vg_vbox_layout_t and vg_hbox_layout_t have spacing as their first
    // field, so vg_vbox_set_spacing works for either type. For plain containers
    // without impl_data, the call is a safe no-op.
    vg_vbox_set_spacing((vg_widget_t *)container, (float)spacing);
}

void rt_container_set_padding(void *container, double padding)
{
    if (container)
    {
        vg_widget_set_padding((vg_widget_t *)container, (float)padding);
    }
}

//=============================================================================
// Widget State Functions
//=============================================================================

int64_t rt_widget_is_hovered(void *widget)
{
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_HOVERED) ? 1 : 0;
}

int64_t rt_widget_is_pressed(void *widget)
{
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_PRESSED) ? 1 : 0;
}

int64_t rt_widget_is_focused(void *widget)
{
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_FOCUSED) ? 1 : 0;
}

// Global for tracking last clicked widget (set by GUI.App.Poll)
static vg_widget_t *g_last_clicked_widget = NULL;

void rt_gui_set_last_clicked(void *widget)
{
    g_last_clicked_widget = (vg_widget_t *)widget;
}

int64_t rt_widget_was_clicked(void *widget)
{
    if (!widget)
        return 0;
    return (g_last_clicked_widget == widget) ? 1 : 0;
}

void rt_widget_set_position(void *widget, int64_t x, int64_t y)
{
    if (widget)
    {
        vg_widget_t *w = (vg_widget_t *)widget;
        w->x = (float)x;
        w->y = (float)y;
    }
}

//=============================================================================
// Dropdown Widget
//=============================================================================

void *rt_dropdown_new(void *parent)
{
    return vg_dropdown_create((vg_widget_t *)parent);
}

int64_t rt_dropdown_add_item(void *dropdown, rt_string text)
{
    if (!dropdown)
        return -1;
    char *ctext = rt_string_to_cstr(text);
    int64_t index = vg_dropdown_add_item((vg_dropdown_t *)dropdown, ctext);
    free(ctext);
    return index;
}

void rt_dropdown_remove_item(void *dropdown, int64_t index)
{
    if (dropdown)
    {
        vg_dropdown_remove_item((vg_dropdown_t *)dropdown, (int)index);
    }
}

void rt_dropdown_clear(void *dropdown)
{
    if (dropdown)
    {
        vg_dropdown_clear((vg_dropdown_t *)dropdown);
    }
}

void rt_dropdown_set_selected(void *dropdown, int64_t index)
{
    if (dropdown)
    {
        vg_dropdown_set_selected((vg_dropdown_t *)dropdown, (int)index);
    }
}

int64_t rt_dropdown_get_selected(void *dropdown)
{
    if (!dropdown)
        return -1;
    return vg_dropdown_get_selected((vg_dropdown_t *)dropdown);
}

rt_string rt_dropdown_get_selected_text(void *dropdown)
{
    if (!dropdown)
        return rt_str_empty();
    const char *text = vg_dropdown_get_selected_text((vg_dropdown_t *)dropdown);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder)
{
    if (!dropdown)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_dropdown_set_placeholder((vg_dropdown_t *)dropdown, ctext);
    free(ctext);
}

//=============================================================================
// Slider Widget
//=============================================================================

void *rt_slider_new(void *parent, int64_t horizontal)
{
    vg_slider_orientation_t orient = horizontal ? VG_SLIDER_HORIZONTAL : VG_SLIDER_VERTICAL;
    return vg_slider_create((vg_widget_t *)parent, orient);
}

void rt_slider_set_value(void *slider, double value)
{
    if (slider)
    {
        vg_slider_set_value((vg_slider_t *)slider, (float)value);
    }
}

double rt_slider_get_value(void *slider)
{
    if (!slider)
        return 0.0;
    return (double)vg_slider_get_value((vg_slider_t *)slider);
}

void rt_slider_set_range(void *slider, double min_val, double max_val)
{
    if (slider)
    {
        vg_slider_set_range((vg_slider_t *)slider, (float)min_val, (float)max_val);
    }
}

void rt_slider_set_step(void *slider, double step)
{
    if (slider)
    {
        vg_slider_set_step((vg_slider_t *)slider, (float)step);
    }
}

//=============================================================================
// ProgressBar Widget
//=============================================================================

void *rt_progressbar_new(void *parent)
{
    return vg_progressbar_create((vg_widget_t *)parent);
}

void rt_progressbar_set_value(void *progress, double value)
{
    if (progress)
    {
        vg_progressbar_set_value((vg_progressbar_t *)progress, (float)value);
    }
}

double rt_progressbar_get_value(void *progress)
{
    if (!progress)
        return 0.0;
    return (double)vg_progressbar_get_value((vg_progressbar_t *)progress);
}

//=============================================================================
// ListBox Widget
//=============================================================================

void *rt_listbox_new(void *parent)
{
    return vg_listbox_create((vg_widget_t *)parent);
}

void *rt_listbox_add_item(void *listbox, rt_string text)
{
    if (!listbox)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_listbox_item_t *item = vg_listbox_add_item((vg_listbox_t *)listbox, ctext, NULL);
    free(ctext);
    return item;
}

void rt_listbox_remove_item(void *listbox, void *item)
{
    if (listbox && item)
    {
        vg_listbox_remove_item((vg_listbox_t *)listbox, (vg_listbox_item_t *)item);
    }
}

void rt_listbox_clear(void *listbox)
{
    if (listbox)
    {
        vg_listbox_clear((vg_listbox_t *)listbox);
    }
}

void rt_listbox_select(void *listbox, void *item)
{
    if (listbox)
    {
        vg_listbox_select((vg_listbox_t *)listbox, (vg_listbox_item_t *)item);
    }
}

void *rt_listbox_get_selected(void *listbox)
{
    if (!listbox)
        return NULL;
    return vg_listbox_get_selected((vg_listbox_t *)listbox);
}

int64_t rt_listbox_get_count(void *listbox)
{
    if (!listbox)
        return 0;
    vg_listbox_t *lb = (vg_listbox_t *)listbox;
    return (int64_t)lb->item_count;
}

int64_t rt_listbox_get_selected_index(void *listbox)
{
    if (!listbox)
        return -1;
    size_t idx = vg_listbox_get_selected_index((vg_listbox_t *)listbox);
    if (idx == (size_t)-1)
        return -1;
    return (int64_t)idx;
}

void rt_listbox_select_index(void *listbox, int64_t index)
{
    if (!listbox || index < 0)
        return;
    vg_listbox_select_index((vg_listbox_t *)listbox, (size_t)index);
}

int64_t rt_listbox_was_selection_changed(void *listbox)
{
    if (!listbox)
        return 0;
    vg_listbox_t *lb = (vg_listbox_t *)listbox;
    // Use a flag to track selection changes
    // This requires checking if selection changed since last query
    // For now, return 0 as a stub - would need tracking state
    (void)lb;
    return 0;
}

rt_string rt_listbox_item_get_text(void *item)
{
    if (!item)
        return rt_const_cstr("");
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (it->text)
        return rt_string_from_bytes(it->text, strlen(it->text));
    return rt_const_cstr("");
}

void rt_listbox_item_set_text(void *item, rt_string text)
{
    if (!item)
        return;
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (it->text)
        free(it->text);
    char *ctext = rt_string_to_cstr(text);
    it->text = ctext; // Takes ownership
}

void rt_listbox_item_set_data(void *item, rt_string data)
{
    if (!item)
        return;
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (it->user_data)
        free(it->user_data);
    char *cdata = rt_string_to_cstr(data);
    it->user_data = cdata; // Takes ownership
}

rt_string rt_listbox_item_get_data(void *item)
{
    if (!item)
        return rt_const_cstr("");
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    char *data = (char *)it->user_data;
    if (data)
        return rt_string_from_bytes(data, strlen(data));
    return rt_const_cstr("");
}

void rt_listbox_set_font(void *listbox, void *font, double size)
{
    if (listbox)
    {
        vg_listbox_set_font((vg_listbox_t *)listbox, (vg_font_t *)font, (float)size);
    }
}

//=============================================================================
// RadioButton Widget
//=============================================================================

void *rt_radiogroup_new(void)
{
    return vg_radiogroup_create();
}

void rt_radiogroup_destroy(void *group)
{
    if (group)
    {
        vg_radiogroup_destroy((vg_radiogroup_t *)group);
    }
}

void *rt_radiobutton_new(void *parent, rt_string text, void *group)
{
    char *ctext = rt_string_to_cstr(text);
    vg_radiobutton_t *radio =
        vg_radiobutton_create((vg_widget_t *)parent, ctext, (vg_radiogroup_t *)group);
    free(ctext);
    return radio;
}

int64_t rt_radiobutton_is_selected(void *radio)
{
    if (!radio)
        return 0;
    return vg_radiobutton_is_selected((vg_radiobutton_t *)radio) ? 1 : 0;
}

void rt_radiobutton_set_selected(void *radio, int64_t selected)
{
    if (radio)
    {
        vg_radiobutton_set_selected((vg_radiobutton_t *)radio, selected != 0);
    }
}

//=============================================================================
// Spinner Widget
//=============================================================================

void *rt_spinner_new(void *parent)
{
    return vg_spinner_create((vg_widget_t *)parent);
}

void rt_spinner_set_value(void *spinner, double value)
{
    if (spinner)
    {
        vg_spinner_set_value((vg_spinner_t *)spinner, value);
    }
}

double rt_spinner_get_value(void *spinner)
{
    if (!spinner)
        return 0.0;
    return vg_spinner_get_value((vg_spinner_t *)spinner);
}

void rt_spinner_set_range(void *spinner, double min_val, double max_val)
{
    if (spinner)
    {
        vg_spinner_set_range((vg_spinner_t *)spinner, min_val, max_val);
    }
}

void rt_spinner_set_step(void *spinner, double step)
{
    if (spinner)
    {
        vg_spinner_set_step((vg_spinner_t *)spinner, step);
    }
}

void rt_spinner_set_decimals(void *spinner, int64_t decimals)
{
    if (spinner)
    {
        vg_spinner_set_decimals((vg_spinner_t *)spinner, (int)decimals);
    }
}

//=============================================================================
// Image Widget
//=============================================================================

void *rt_image_new(void *parent)
{
    return vg_image_create((vg_widget_t *)parent);
}

void rt_image_set_pixels(void *image, void *pixels, int64_t width, int64_t height)
{
    if (image && pixels)
    {
        vg_image_set_pixels((vg_image_t *)image, (const uint8_t *)pixels, (int)width, (int)height);
    }
}

void rt_image_clear(void *image)
{
    if (image)
    {
        vg_image_clear((vg_image_t *)image);
    }
}

void rt_image_set_scale_mode(void *image, int64_t mode)
{
    if (image)
    {
        vg_image_set_scale_mode((vg_image_t *)image, (vg_image_scale_t)mode);
    }
}

void rt_image_set_opacity(void *image, double opacity)
{
    if (image)
    {
        vg_image_set_opacity((vg_image_t *)image, (float)opacity);
    }
}

//=============================================================================
// FloatingPanel Widget
//=============================================================================

void *rt_floatingpanel_new(void *root)
{
    return vg_floatingpanel_create((vg_widget_t *)root);
}

void rt_floatingpanel_set_position(void *panel, double x, double y)
{
    if (panel)
        vg_floatingpanel_set_position((vg_floatingpanel_t *)panel, (float)x, (float)y);
}

void rt_floatingpanel_set_size(void *panel, double w, double h)
{
    if (panel)
        vg_floatingpanel_set_size((vg_floatingpanel_t *)panel, (float)w, (float)h);
}

void rt_floatingpanel_set_visible(void *panel, int64_t visible)
{
    if (panel)
        vg_floatingpanel_set_visible((vg_floatingpanel_t *)panel, (int)visible);
}

void rt_floatingpanel_add_child(void *panel, void *child)
{
    if (panel && child)
        vg_floatingpanel_add_child((vg_floatingpanel_t *)panel, (vg_widget_t *)child);
}

//=============================================================================
