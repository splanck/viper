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
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// TabBar Widget
//=============================================================================

void *rt_tabbar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tabbar = vg_tabbar_create((vg_widget_t *)parent);
    if (tabbar) {
        rt_gui_ensure_default_font();
        if (s_current_app && s_current_app->default_font)
            vg_tabbar_set_font(
                tabbar, s_current_app->default_font, s_current_app->default_font_size);
    }
    return tabbar;
}

void *rt_tabbar_add_tab(void *tabbar, rt_string title, int64_t closable) {
    RT_ASSERT_MAIN_THREAD();
    if (!tabbar)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_t *tab = vg_tabbar_add_tab((vg_tabbar_t *)tabbar, ctitle, closable != 0);
    free(ctitle);
    return tab;
}

/// @brief Perform tabbar remove tab operation.
/// @param tabbar
/// @param tab
void rt_tabbar_remove_tab(void *tabbar, void *tab) {
    RT_ASSERT_MAIN_THREAD();
    if (tabbar && tab) {
        vg_tabbar_remove_tab((vg_tabbar_t *)tabbar, (vg_tab_t *)tab);
    }
}

/// @brief Perform tabbar set active operation.
/// @param tabbar
/// @param tab
void rt_tabbar_set_active(void *tabbar, void *tab) {
    RT_ASSERT_MAIN_THREAD();
    if (tabbar) {
        vg_tabbar_set_active((vg_tabbar_t *)tabbar, (vg_tab_t *)tab);
    }
}

/// @brief Perform tab set title operation.
/// @param tab
/// @param title
void rt_tab_set_title(void *tab, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    if (!tab)
        return;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_set_title((vg_tab_t *)tab, ctitle);
    free(ctitle);
}

/// @brief Perform tab set modified operation.
/// @param tab
/// @param modified
void rt_tab_set_modified(void *tab, int64_t modified) {
    RT_ASSERT_MAIN_THREAD();
    if (tab) {
        vg_tab_set_modified((vg_tab_t *)tab, modified != 0);
    }
}

void *rt_tabbar_get_active(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    return tabbar ? ((vg_tabbar_t *)tabbar)->active_tab : NULL;
}

/// @brief Perform tabbar get active index operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_get_active_index(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    if (!tabbar)
        return -1;
    vg_tabbar_t *tb = (vg_tabbar_t *)tabbar;
    return vg_tabbar_get_tab_index(tb, tb->active_tab);
}

/// @brief Perform tabbar was changed operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_was_changed(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    if (!tabbar)
        return 0;
    vg_tabbar_t *tb = (vg_tabbar_t *)tabbar;
    if (tb->active_tab != tb->prev_active_tab) {
        tb->prev_active_tab = tb->active_tab;
        return 1;
    }
    return 0;
}

/// @brief Perform tabbar get tab count operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_get_tab_count(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    return tabbar ? ((vg_tabbar_t *)tabbar)->tab_count : 0;
}

/// @brief Perform tabbar was close clicked operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_was_close_clicked(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    if (!tabbar)
        return 0;
    return ((vg_tabbar_t *)tabbar)->close_clicked_tab ? 1 : 0;
}

/// @brief Perform tabbar get close clicked index operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_get_close_clicked_index(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    if (!tabbar)
        return -1;
    vg_tabbar_t *tb = (vg_tabbar_t *)tabbar;
    if (!tb->close_clicked_tab)
        return -1;
    int index = vg_tabbar_get_tab_index(tb, tb->close_clicked_tab);
    tb->close_clicked_tab = NULL;
    return index;
}

void *rt_tabbar_get_tab_at(void *tabbar, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (!tabbar)
        return NULL;
    return vg_tabbar_get_tab_at((vg_tabbar_t *)tabbar, (int)index);
}

/// @brief Perform tabbar set auto close operation.
/// @param tabbar
/// @param auto_close
void rt_tabbar_set_auto_close(void *tabbar, int64_t auto_close) {
    RT_ASSERT_MAIN_THREAD();
    if (tabbar) {
        ((vg_tabbar_t *)tabbar)->auto_close = auto_close != 0;
    }
}

//=============================================================================
// SplitPane Widget
//=============================================================================

void *rt_splitpane_new(void *parent, int64_t horizontal) {
    RT_ASSERT_MAIN_THREAD();
    vg_split_direction_t direction = horizontal ? VG_SPLIT_HORIZONTAL : VG_SPLIT_VERTICAL;
    return vg_splitpane_create((vg_widget_t *)parent, direction);
}

/// @brief Perform splitpane set position operation.
/// @param split
/// @param position
void rt_splitpane_set_position(void *split, double position) {
    RT_ASSERT_MAIN_THREAD();
    if (split) {
        vg_splitpane_set_position((vg_splitpane_t *)split, (float)position);
    }
}

// BINDING-006: SplitPane position query
/// @brief Perform splitpane get position operation.
/// @param split
/// @return Result value.
double rt_splitpane_get_position(void *split) {
    RT_ASSERT_MAIN_THREAD();
    if (!split)
        return 0.5;
    return (double)vg_splitpane_get_position((vg_splitpane_t *)split);
}

void *rt_splitpane_get_first(void *split) {
    RT_ASSERT_MAIN_THREAD();
    if (!split)
        return NULL;
    return vg_splitpane_get_first((vg_splitpane_t *)split);
}

void *rt_splitpane_get_second(void *split) {
    RT_ASSERT_MAIN_THREAD();
    if (!split)
        return NULL;
    return vg_splitpane_get_second((vg_splitpane_t *)split);
}

//=============================================================================
// CodeEditor Widget
//=============================================================================

void *rt_codeeditor_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *editor = vg_codeeditor_create((vg_widget_t *)parent);
    if (editor && s_current_app) {
        rt_gui_ensure_default_font();
        if (s_current_app->default_font) {
            vg_codeeditor_set_font(
                editor, s_current_app->default_font, s_current_app->default_font_size);
        }
    }
    return editor;
}

/// @brief Perform codeeditor set text operation.
/// @param editor
/// @param text
void rt_codeeditor_set_text(void *editor, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!editor)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_codeeditor_set_text((vg_codeeditor_t *)editor, ctext);
    free(ctext);
}

/// @brief Perform codeeditor get text operation.
/// @param editor
/// @return Result value.
rt_string rt_codeeditor_get_text(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    if (!editor)
        return rt_str_empty();
    char *text = vg_codeeditor_get_text((vg_codeeditor_t *)editor);
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

/// @brief Perform codeeditor get selected text operation.
/// @param editor
/// @return Result value.
rt_string rt_codeeditor_get_selected_text(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    if (!editor)
        return rt_str_empty();
    char *text = vg_codeeditor_get_selection((vg_codeeditor_t *)editor);
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

/// @brief Perform codeeditor set cursor operation.
/// @param editor
/// @param line
/// @param col
void rt_codeeditor_set_cursor(void *editor, int64_t line, int64_t col) {
    RT_ASSERT_MAIN_THREAD();
    if (editor) {
        vg_codeeditor_set_cursor((vg_codeeditor_t *)editor, (int)line, (int)col);
    }
}

/// @brief Perform codeeditor scroll to line operation.
/// @param editor
/// @param line
void rt_codeeditor_scroll_to_line(void *editor, int64_t line) {
    RT_ASSERT_MAIN_THREAD();
    if (editor) {
        vg_codeeditor_scroll_to_line((vg_codeeditor_t *)editor, (int)line);
    }
}

/// @brief Perform codeeditor get line count operation.
/// @param editor
/// @return Result value.
int64_t rt_codeeditor_get_line_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    if (!editor)
        return 0;
    return vg_codeeditor_get_line_count((vg_codeeditor_t *)editor);
}

/// @brief Perform codeeditor is modified operation.
/// @param editor
/// @return Result value.
int64_t rt_codeeditor_is_modified(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    if (!editor)
        return 0;
    return vg_codeeditor_is_modified((vg_codeeditor_t *)editor) ? 1 : 0;
}

/// @brief Perform codeeditor clear modified operation.
/// @param editor
void rt_codeeditor_clear_modified(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    if (editor) {
        vg_codeeditor_clear_modified((vg_codeeditor_t *)editor);
    }
}

/// @brief Perform codeeditor set font operation.
/// @param editor
/// @param font
/// @param size
void rt_codeeditor_set_font(void *editor, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (editor) {
        vg_codeeditor_set_font((vg_codeeditor_t *)editor, (vg_font_t *)font, (float)size);
    }
}

/// @brief Perform codeeditor get font size operation.
/// @param editor
/// @return Result value.
double rt_codeeditor_get_font_size(void *editor) {
    RT_ASSERT_MAIN_THREAD();
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

/// @brief Perform codeeditor set font size operation.
/// @param editor
/// @param size
void rt_codeeditor_set_font_size(void *editor, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (!editor)
        return;
    vg_codeeditor_t *ed = (vg_codeeditor_t *)editor;
    if (size > 0.0) {
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

/// @brief Perform theme apply hidpi scale operation.
void rt_theme_apply_hidpi_scale(void) {
    if (!s_current_app || !s_current_app->window)
        return;
    float _s = vgfx_window_get_scale(s_current_app->window);
    if (_s <= 0.0f)
        _s = 1.0f;
    vg_theme_t *_t = vg_theme_get_current();
    if (!_t)
        return;
    _t->ui_scale = _s;
    /* Re-apply scaling from the base theme values (theme_dark/light return
     * fresh unscaled copies, so the values are unscaled at this point). */
    _t->typography.size_small *= _s;
    _t->typography.size_normal *= _s;
    _t->typography.size_large *= _s;
    _t->typography.size_heading *= _s;
    _t->spacing.xs *= _s;
    _t->spacing.sm *= _s;
    _t->spacing.md *= _s;
    _t->spacing.lg *= _s;
    _t->spacing.xl *= _s;
    _t->button.height *= _s;
    _t->button.padding_h *= _s;
    _t->input.height *= _s;
    _t->input.padding_h *= _s;
    _t->scrollbar.width *= _s;
}

/// @brief Perform theme set dark operation.
void rt_theme_set_dark(void) {
    RT_ASSERT_MAIN_THREAD();
    vg_theme_set_current(vg_theme_dark());
    rt_theme_apply_hidpi_scale();
}

/// @brief Perform theme set light operation.
void rt_theme_set_light(void) {
    RT_ASSERT_MAIN_THREAD();
    vg_theme_set_current(vg_theme_light());
    rt_theme_apply_hidpi_scale();
}

/// @brief Perform theme get name operation.
/// @return Result value.
rt_string rt_theme_get_name(void) {
    RT_ASSERT_MAIN_THREAD();
    // Query the vg layer directly rather than maintaining a shadow variable.
    vg_theme_t *current = vg_theme_get_current();
    const char *name = (current == vg_theme_dark()) ? "dark" : "light";
    return rt_string_from_bytes(name, strlen(name));
}

//=============================================================================
// Layout Functions
//=============================================================================

void *rt_vbox_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_vbox_create(0.0f);
}

void *rt_hbox_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_hbox_create(0.0f);
}

/// @brief Perform container set spacing operation.
/// @param container
/// @param spacing
void rt_container_set_spacing(void *container, double spacing) {
    RT_ASSERT_MAIN_THREAD();
    if (!container)
        return;
    // Both vg_vbox_layout_t and vg_hbox_layout_t have spacing as their first
    // field, so vg_vbox_set_spacing works for either type. For plain containers
    // without impl_data, the call is a safe no-op.
    vg_vbox_set_spacing((vg_widget_t *)container, (float)spacing);
}

/// @brief Perform container set padding operation.
/// @param container
/// @param padding
void rt_container_set_padding(void *container, double padding) {
    RT_ASSERT_MAIN_THREAD();
    if (container) {
        vg_widget_set_padding((vg_widget_t *)container, (float)padding);
    }
}

//=============================================================================
// Widget State Functions
//=============================================================================

/// @brief Perform widget is hovered operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_hovered(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_HOVERED) ? 1 : 0;
}

/// @brief Perform widget is pressed operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_pressed(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_PRESSED) ? 1 : 0;
}

/// @brief Perform widget is focused operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_focused(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_FOCUSED) ? 1 : 0;
}

// Global for tracking last clicked widget (set by GUI.App.Poll)
static vg_widget_t *g_last_clicked_widget = NULL;

/// @brief Set the last clicked value.
/// @param widget
void rt_gui_set_last_clicked(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    g_last_clicked_widget = (vg_widget_t *)widget;
}

/// @brief Perform widget was clicked operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_was_clicked(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (g_last_clicked_widget == widget) ? 1 : 0;
}

/// @brief Perform widget set position operation.
/// @param widget
/// @param x
/// @param y
void rt_widget_set_position(void *widget, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    if (widget) {
        vg_widget_t *w = (vg_widget_t *)widget;
        w->x = (float)x;
        w->y = (float)y;
    }
}

//=============================================================================
// Dropdown Widget
//=============================================================================

void *rt_dropdown_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    return vg_dropdown_create((vg_widget_t *)parent);
}

/// @brief Perform dropdown add item operation.
/// @param dropdown
/// @param text
/// @return Result value.
int64_t rt_dropdown_add_item(void *dropdown, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!dropdown)
        return -1;
    char *ctext = rt_string_to_cstr(text);
    int64_t index = vg_dropdown_add_item((vg_dropdown_t *)dropdown, ctext);
    free(ctext);
    return index;
}

/// @brief Perform dropdown remove item operation.
/// @param dropdown
/// @param index
void rt_dropdown_remove_item(void *dropdown, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (dropdown) {
        vg_dropdown_remove_item((vg_dropdown_t *)dropdown, (int)index);
    }
}

/// @brief Perform dropdown clear operation.
/// @param dropdown
void rt_dropdown_clear(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    if (dropdown) {
        vg_dropdown_clear((vg_dropdown_t *)dropdown);
    }
}

/// @brief Perform dropdown set selected operation.
/// @param dropdown
/// @param index
void rt_dropdown_set_selected(void *dropdown, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (dropdown) {
        vg_dropdown_set_selected((vg_dropdown_t *)dropdown, (int)index);
    }
}

/// @brief Perform dropdown get selected operation.
/// @param dropdown
/// @return Result value.
int64_t rt_dropdown_get_selected(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    if (!dropdown)
        return -1;
    return vg_dropdown_get_selected((vg_dropdown_t *)dropdown);
}

/// @brief Perform dropdown get selected text operation.
/// @param dropdown
/// @return Result value.
rt_string rt_dropdown_get_selected_text(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    if (!dropdown)
        return rt_str_empty();
    const char *text = vg_dropdown_get_selected_text((vg_dropdown_t *)dropdown);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

/// @brief Perform dropdown set placeholder operation.
/// @param dropdown
/// @param placeholder
void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder) {
    RT_ASSERT_MAIN_THREAD();
    if (!dropdown)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_dropdown_set_placeholder((vg_dropdown_t *)dropdown, ctext);
    free(ctext);
}

//=============================================================================
// Slider Widget
//=============================================================================

void *rt_slider_new(void *parent, int64_t horizontal) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_orientation_t orient = horizontal ? VG_SLIDER_HORIZONTAL : VG_SLIDER_VERTICAL;
    return vg_slider_create((vg_widget_t *)parent, orient);
}

/// @brief Perform slider set value operation.
/// @param slider
/// @param value
void rt_slider_set_value(void *slider, double value) {
    RT_ASSERT_MAIN_THREAD();
    if (slider) {
        vg_slider_set_value((vg_slider_t *)slider, (float)value);
    }
}

/// @brief Perform slider get value operation.
/// @param slider
/// @return Result value.
double rt_slider_get_value(void *slider) {
    RT_ASSERT_MAIN_THREAD();
    if (!slider)
        return 0.0;
    return (double)vg_slider_get_value((vg_slider_t *)slider);
}

/// @brief Perform slider set range operation.
/// @param slider
/// @param min_val
/// @param max_val
void rt_slider_set_range(void *slider, double min_val, double max_val) {
    RT_ASSERT_MAIN_THREAD();
    if (slider) {
        vg_slider_set_range((vg_slider_t *)slider, (float)min_val, (float)max_val);
    }
}

/// @brief Perform slider set step operation.
/// @param slider
/// @param step
void rt_slider_set_step(void *slider, double step) {
    RT_ASSERT_MAIN_THREAD();
    if (slider) {
        vg_slider_set_step((vg_slider_t *)slider, (float)step);
    }
}

//=============================================================================
// ProgressBar Widget
//=============================================================================

void *rt_progressbar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    return vg_progressbar_create((vg_widget_t *)parent);
}

/// @brief Perform progressbar set value operation.
/// @param progress
/// @param value
void rt_progressbar_set_value(void *progress, double value) {
    RT_ASSERT_MAIN_THREAD();
    if (progress) {
        vg_progressbar_set_value((vg_progressbar_t *)progress, (float)value);
    }
}

/// @brief Perform progressbar get value operation.
/// @param progress
/// @return Result value.
double rt_progressbar_get_value(void *progress) {
    RT_ASSERT_MAIN_THREAD();
    if (!progress)
        return 0.0;
    return (double)vg_progressbar_get_value((vg_progressbar_t *)progress);
}

//=============================================================================
// ListBox Widget
//=============================================================================

void *rt_listbox_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    return vg_listbox_create((vg_widget_t *)parent);
}

void *rt_listbox_add_item(void *listbox, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!listbox)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_listbox_item_t *item = vg_listbox_add_item((vg_listbox_t *)listbox, ctext, NULL);
    free(ctext);
    return item;
}

/// @brief Perform listbox remove item operation.
/// @param listbox
/// @param item
void rt_listbox_remove_item(void *listbox, void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (listbox && item) {
        vg_listbox_remove_item((vg_listbox_t *)listbox, (vg_listbox_item_t *)item);
    }
}

/// @brief Perform listbox clear operation.
/// @param listbox
void rt_listbox_clear(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    if (listbox) {
        vg_listbox_clear((vg_listbox_t *)listbox);
    }
}

/// @brief Perform listbox select operation.
/// @param listbox
/// @param item
void rt_listbox_select(void *listbox, void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (listbox) {
        vg_listbox_select((vg_listbox_t *)listbox, (vg_listbox_item_t *)item);
    }
}

void *rt_listbox_get_selected(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    if (!listbox)
        return NULL;
    return vg_listbox_get_selected((vg_listbox_t *)listbox);
}

/// @brief Perform listbox get count operation.
/// @param listbox
/// @return Result value.
int64_t rt_listbox_get_count(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    if (!listbox)
        return 0;
    vg_listbox_t *lb = (vg_listbox_t *)listbox;
    return (int64_t)lb->item_count;
}

/// @brief Perform listbox get selected index operation.
/// @param listbox
/// @return Result value.
int64_t rt_listbox_get_selected_index(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    if (!listbox)
        return -1;
    size_t idx = vg_listbox_get_selected_index((vg_listbox_t *)listbox);
    if (idx == (size_t)-1)
        return -1;
    return (int64_t)idx;
}

/// @brief Perform listbox select index operation.
/// @param listbox
/// @param index
void rt_listbox_select_index(void *listbox, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (!listbox || index < 0)
        return;
    vg_listbox_select_index((vg_listbox_t *)listbox, (size_t)index);
}

/// @brief Perform listbox was selection changed operation.
/// @param listbox
/// @return Result value.
int64_t rt_listbox_was_selection_changed(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    if (!listbox)
        return 0;
    vg_listbox_t *lb = (vg_listbox_t *)listbox;

    // Per-instance selection tracking using prev_selected field
    // (matches the pattern used by rt_tabbar_was_changed / prev_active_tab).
    if (lb->selected != lb->prev_selected) {
        lb->prev_selected = lb->selected;
        return 1;
    }
    return 0;
}

/// @brief Perform listbox item get text operation.
/// @param item
/// @return Result value.
rt_string rt_listbox_item_get_text(void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (it->text)
        return rt_string_from_bytes(it->text, strlen(it->text));
    return rt_str_empty();
}

/// @brief Perform listbox item set text operation.
/// @param item
/// @param text
void rt_listbox_item_set_text(void *item, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (it->text)
        free(it->text);
    char *ctext = rt_string_to_cstr(text);
    it->text = ctext; // Takes ownership
}

/// @brief Perform listbox item set data operation.
/// @param item
/// @param data
void rt_listbox_item_set_data(void *item, rt_string data) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (it->user_data)
        free(it->user_data);
    char *cdata = rt_string_to_cstr(data);
    it->user_data = cdata; // Takes ownership
}

/// @brief Perform listbox item get data operation.
/// @param item
/// @return Result value.
rt_string rt_listbox_item_get_data(void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    char *data = (char *)it->user_data;
    if (data)
        return rt_string_from_bytes(data, strlen(data));
    return rt_str_empty();
}

/// @brief Perform listbox set font operation.
/// @param listbox
/// @param font
/// @param size
void rt_listbox_set_font(void *listbox, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (listbox) {
        vg_listbox_set_font((vg_listbox_t *)listbox, (vg_font_t *)font, (float)size);
    }
}

//=============================================================================
// RadioButton Widget
//=============================================================================

void *rt_radiogroup_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_radiogroup_create();
}

/// @brief Perform radiogroup destroy operation.
/// @param group
void rt_radiogroup_destroy(void *group) {
    RT_ASSERT_MAIN_THREAD();
    if (group) {
        vg_radiogroup_destroy((vg_radiogroup_t *)group);
    }
}

void *rt_radiobutton_new(void *parent, rt_string text, void *group) {
    RT_ASSERT_MAIN_THREAD();
    char *ctext = rt_string_to_cstr(text);
    vg_radiobutton_t *radio =
        vg_radiobutton_create((vg_widget_t *)parent, ctext, (vg_radiogroup_t *)group);
    free(ctext);
    return radio;
}

/// @brief Perform radiobutton is selected operation.
/// @param radio
/// @return Result value.
int64_t rt_radiobutton_is_selected(void *radio) {
    RT_ASSERT_MAIN_THREAD();
    if (!radio)
        return 0;
    return vg_radiobutton_is_selected((vg_radiobutton_t *)radio) ? 1 : 0;
}

/// @brief Perform radiobutton set selected operation.
/// @param radio
/// @param selected
void rt_radiobutton_set_selected(void *radio, int64_t selected) {
    RT_ASSERT_MAIN_THREAD();
    if (radio) {
        vg_radiobutton_set_selected((vg_radiobutton_t *)radio, selected != 0);
    }
}

//=============================================================================
// Spinner Widget
//=============================================================================

void *rt_spinner_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    return vg_spinner_create((vg_widget_t *)parent);
}

/// @brief Perform spinner set value operation.
/// @param spinner
/// @param value
void rt_spinner_set_value(void *spinner, double value) {
    RT_ASSERT_MAIN_THREAD();
    if (spinner) {
        vg_spinner_set_value((vg_spinner_t *)spinner, value);
    }
}

/// @brief Perform spinner get value operation.
/// @param spinner
/// @return Result value.
double rt_spinner_get_value(void *spinner) {
    RT_ASSERT_MAIN_THREAD();
    if (!spinner)
        return 0.0;
    return vg_spinner_get_value((vg_spinner_t *)spinner);
}

/// @brief Perform spinner set range operation.
/// @param spinner
/// @param min_val
/// @param max_val
void rt_spinner_set_range(void *spinner, double min_val, double max_val) {
    RT_ASSERT_MAIN_THREAD();
    if (spinner) {
        vg_spinner_set_range((vg_spinner_t *)spinner, min_val, max_val);
    }
}

/// @brief Perform spinner set step operation.
/// @param spinner
/// @param step
void rt_spinner_set_step(void *spinner, double step) {
    RT_ASSERT_MAIN_THREAD();
    if (spinner) {
        vg_spinner_set_step((vg_spinner_t *)spinner, step);
    }
}

/// @brief Perform spinner set decimals operation.
/// @param spinner
/// @param decimals
void rt_spinner_set_decimals(void *spinner, int64_t decimals) {
    RT_ASSERT_MAIN_THREAD();
    if (spinner) {
        vg_spinner_set_decimals((vg_spinner_t *)spinner, (int)decimals);
    }
}

//=============================================================================
// Image Widget
//=============================================================================

void *rt_image_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    return vg_image_create((vg_widget_t *)parent);
}

/// @brief Perform image set pixels operation.
/// @param image
/// @param pixels
/// @param width
/// @param height
void rt_image_set_pixels(void *image, void *pixels, int64_t width, int64_t height) {
    RT_ASSERT_MAIN_THREAD();
    if (image && pixels) {
        vg_image_set_pixels((vg_image_t *)image, (const uint8_t *)pixels, (int)width, (int)height);
    }
}

/// @brief Perform image clear operation.
/// @param image
void rt_image_clear(void *image) {
    RT_ASSERT_MAIN_THREAD();
    if (image) {
        vg_image_clear((vg_image_t *)image);
    }
}

/// @brief Perform image set scale mode operation.
/// @param image
/// @param mode
void rt_image_set_scale_mode(void *image, int64_t mode) {
    RT_ASSERT_MAIN_THREAD();
    if (image) {
        vg_image_set_scale_mode((vg_image_t *)image, (vg_image_scale_t)mode);
    }
}

/// @brief Perform image set opacity operation.
/// @param image
/// @param opacity
void rt_image_set_opacity(void *image, double opacity) {
    RT_ASSERT_MAIN_THREAD();
    if (image) {
        vg_image_set_opacity((vg_image_t *)image, (float)opacity);
    }
}

//=============================================================================
// FloatingPanel Widget
//=============================================================================

void *rt_floatingpanel_new(void *root) {
    RT_ASSERT_MAIN_THREAD();
    return vg_floatingpanel_create((vg_widget_t *)root);
}

/// @brief Perform floatingpanel set position operation.
/// @param panel
/// @param x
/// @param y
void rt_floatingpanel_set_position(void *panel, double x, double y) {
    RT_ASSERT_MAIN_THREAD();
    if (panel)
        vg_floatingpanel_set_position((vg_floatingpanel_t *)panel, (float)x, (float)y);
}

/// @brief Perform floatingpanel set size operation.
/// @param panel
/// @param w
/// @param h
void rt_floatingpanel_set_size(void *panel, double w, double h) {
    RT_ASSERT_MAIN_THREAD();
    if (panel)
        vg_floatingpanel_set_size((vg_floatingpanel_t *)panel, (float)w, (float)h);
}

/// @brief Perform floatingpanel set visible operation.
/// @param panel
/// @param visible
void rt_floatingpanel_set_visible(void *panel, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    if (panel)
        vg_floatingpanel_set_visible((vg_floatingpanel_t *)panel, (int)visible);
}

/// @brief Perform floatingpanel add child operation.
/// @param panel
/// @param child
void rt_floatingpanel_add_child(void *panel, void *child) {
    RT_ASSERT_MAIN_THREAD();
    if (panel && child)
        vg_floatingpanel_add_child((vg_floatingpanel_t *)panel, (vg_widget_t *)child);
}

#else /* !VIPER_ENABLE_GRAPHICS */

void *rt_tabbar_new(void *parent) {
    (void)parent;
    return NULL;
}

void *rt_tabbar_add_tab(void *tabbar, rt_string title, int64_t closable) {
    (void)tabbar;
    (void)title;
    (void)closable;
    return NULL;
}

/// @brief Perform tabbar remove tab operation.
/// @param tabbar
/// @param tab
void rt_tabbar_remove_tab(void *tabbar, void *tab) {
    (void)tabbar;
    (void)tab;
}

/// @brief Perform tabbar set active operation.
/// @param tabbar
/// @param tab
void rt_tabbar_set_active(void *tabbar, void *tab) {
    (void)tabbar;
    (void)tab;
}

/// @brief Perform tab set title operation.
/// @param tab
/// @param title
void rt_tab_set_title(void *tab, rt_string title) {
    (void)tab;
    (void)title;
}

/// @brief Perform tab set modified operation.
/// @param tab
/// @param modified
void rt_tab_set_modified(void *tab, int64_t modified) {
    (void)tab;
    (void)modified;
}

void *rt_tabbar_get_active(void *tabbar) {
    (void)tabbar;
    return NULL;
}

/// @brief Perform tabbar get active index operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_get_active_index(void *tabbar) {
    (void)tabbar;
    return -1;
}

/// @brief Perform tabbar was changed operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_was_changed(void *tabbar) {
    (void)tabbar;
    return 0;
}

/// @brief Perform tabbar get tab count operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_get_tab_count(void *tabbar) {
    (void)tabbar;
    return 0;
}

/// @brief Perform tabbar was close clicked operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_was_close_clicked(void *tabbar) {
    (void)tabbar;
    return 0;
}

/// @brief Perform tabbar get close clicked index operation.
/// @param tabbar
/// @return Result value.
int64_t rt_tabbar_get_close_clicked_index(void *tabbar) {
    (void)tabbar;
    return -1;
}

void *rt_tabbar_get_tab_at(void *tabbar, int64_t index) {
    (void)tabbar;
    (void)index;
    return NULL;
}

/// @brief Perform tabbar set auto close operation.
/// @param tabbar
/// @param auto_close
void rt_tabbar_set_auto_close(void *tabbar, int64_t auto_close) {
    (void)tabbar;
    (void)auto_close;
}

void *rt_splitpane_new(void *parent, int64_t horizontal) {
    (void)parent;
    (void)horizontal;
    return NULL;
}

/// @brief Perform splitpane set position operation.
/// @param split
/// @param position
void rt_splitpane_set_position(void *split, double position) {
    (void)split;
    (void)position;
}

/// @brief Perform splitpane get position operation.
/// @param split
/// @return Result value.
double rt_splitpane_get_position(void *split) {
    (void)split;
    return 0.5;
}

void *rt_splitpane_get_first(void *split) {
    (void)split;
    return NULL;
}

void *rt_splitpane_get_second(void *split) {
    (void)split;
    return NULL;
}

void *rt_codeeditor_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Perform codeeditor set text operation.
/// @param editor
/// @param text
void rt_codeeditor_set_text(void *editor, rt_string text) {
    (void)editor;
    (void)text;
}

/// @brief Perform codeeditor get text operation.
/// @param editor
/// @return Result value.
rt_string rt_codeeditor_get_text(void *editor) {
    (void)editor;
    return rt_str_empty();
}

/// @brief Perform codeeditor get selected text operation.
/// @param editor
/// @return Result value.
rt_string rt_codeeditor_get_selected_text(void *editor) {
    (void)editor;
    return rt_str_empty();
}

/// @brief Perform codeeditor set cursor operation.
/// @param editor
/// @param line
/// @param col
void rt_codeeditor_set_cursor(void *editor, int64_t line, int64_t col) {
    (void)editor;
    (void)line;
    (void)col;
}

/// @brief Perform codeeditor scroll to line operation.
/// @param editor
/// @param line
void rt_codeeditor_scroll_to_line(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

/// @brief Perform codeeditor get line count operation.
/// @param editor
/// @return Result value.
int64_t rt_codeeditor_get_line_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Perform codeeditor is modified operation.
/// @param editor
/// @return Result value.
int64_t rt_codeeditor_is_modified(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Perform codeeditor clear modified operation.
/// @param editor
void rt_codeeditor_clear_modified(void *editor) {
    (void)editor;
}

/// @brief Perform codeeditor set font operation.
/// @param editor
/// @param font
/// @param size
void rt_codeeditor_set_font(void *editor, void *font, double size) {
    (void)editor;
    (void)font;
    (void)size;
}

/// @brief Perform codeeditor get font size operation.
/// @param editor
/// @return Result value.
double rt_codeeditor_get_font_size(void *editor) {
    (void)editor;
    return 14.0;
}

/// @brief Perform codeeditor set font size operation.
/// @param editor
/// @param size
void rt_codeeditor_set_font_size(void *editor, double size) {
    (void)editor;
    (void)size;
}

/// @brief Perform theme set dark operation.
void rt_theme_set_dark(void) {}

/// @brief Perform theme set light operation.
void rt_theme_set_light(void) {}

/// @brief Perform theme get name operation.
/// @return Result value.
rt_string rt_theme_get_name(void) {
    return rt_string_from_bytes("dark", 4);
}

void *rt_vbox_new(void) {
    return NULL;
}

void *rt_hbox_new(void) {
    return NULL;
}

/// @brief Perform container set spacing operation.
/// @param container
/// @param spacing
void rt_container_set_spacing(void *container, double spacing) {
    (void)container;
    (void)spacing;
}

/// @brief Perform container set padding operation.
/// @param container
/// @param padding
void rt_container_set_padding(void *container, double padding) {
    (void)container;
    (void)padding;
}

/// @brief Perform widget is hovered operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_hovered(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Perform widget is pressed operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_pressed(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Perform widget is focused operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_is_focused(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Set the last clicked value.
/// @param widget
void rt_gui_set_last_clicked(void *widget) {
    (void)widget;
}

/// @brief Perform widget was clicked operation.
/// @param widget
/// @return Result value.
int64_t rt_widget_was_clicked(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Perform widget set position operation.
/// @param widget
/// @param x
/// @param y
void rt_widget_set_position(void *widget, int64_t x, int64_t y) {
    (void)widget;
    (void)x;
    (void)y;
}

void *rt_dropdown_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Perform dropdown add item operation.
/// @param dropdown
/// @param text
/// @return Result value.
int64_t rt_dropdown_add_item(void *dropdown, rt_string text) {
    (void)dropdown;
    (void)text;
    return -1;
}

/// @brief Perform dropdown remove item operation.
/// @param dropdown
/// @param index
void rt_dropdown_remove_item(void *dropdown, int64_t index) {
    (void)dropdown;
    (void)index;
}

/// @brief Perform dropdown clear operation.
/// @param dropdown
void rt_dropdown_clear(void *dropdown) {
    (void)dropdown;
}

/// @brief Perform dropdown set selected operation.
/// @param dropdown
/// @param index
void rt_dropdown_set_selected(void *dropdown, int64_t index) {
    (void)dropdown;
    (void)index;
}

/// @brief Perform dropdown get selected operation.
/// @param dropdown
/// @return Result value.
int64_t rt_dropdown_get_selected(void *dropdown) {
    (void)dropdown;
    return -1;
}

/// @brief Perform dropdown get selected text operation.
/// @param dropdown
/// @return Result value.
rt_string rt_dropdown_get_selected_text(void *dropdown) {
    (void)dropdown;
    return rt_str_empty();
}

/// @brief Perform dropdown set placeholder operation.
/// @param dropdown
/// @param placeholder
void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder) {
    (void)dropdown;
    (void)placeholder;
}

void *rt_slider_new(void *parent, int64_t horizontal) {
    (void)parent;
    (void)horizontal;
    return NULL;
}

/// @brief Perform slider set value operation.
/// @param slider
/// @param value
void rt_slider_set_value(void *slider, double value) {
    (void)slider;
    (void)value;
}

/// @brief Perform slider get value operation.
/// @param slider
/// @return Result value.
double rt_slider_get_value(void *slider) {
    (void)slider;
    return 0.0;
}

/// @brief Perform slider set range operation.
/// @param slider
/// @param min_val
/// @param max_val
void rt_slider_set_range(void *slider, double min_val, double max_val) {
    (void)slider;
    (void)min_val;
    (void)max_val;
}

/// @brief Perform slider set step operation.
/// @param slider
/// @param step
void rt_slider_set_step(void *slider, double step) {
    (void)slider;
    (void)step;
}

void *rt_progressbar_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Perform progressbar set value operation.
/// @param progress
/// @param value
void rt_progressbar_set_value(void *progress, double value) {
    (void)progress;
    (void)value;
}

/// @brief Perform progressbar get value operation.
/// @param progress
/// @return Result value.
double rt_progressbar_get_value(void *progress) {
    (void)progress;
    return 0.0;
}

void *rt_listbox_new(void *parent) {
    (void)parent;
    return NULL;
}

void *rt_listbox_add_item(void *listbox, rt_string text) {
    (void)listbox;
    (void)text;
    return NULL;
}

/// @brief Perform listbox remove item operation.
/// @param listbox
/// @param item
void rt_listbox_remove_item(void *listbox, void *item) {
    (void)listbox;
    (void)item;
}

/// @brief Perform listbox clear operation.
/// @param listbox
void rt_listbox_clear(void *listbox) {
    (void)listbox;
}

/// @brief Perform listbox select operation.
/// @param listbox
/// @param item
void rt_listbox_select(void *listbox, void *item) {
    (void)listbox;
    (void)item;
}

void *rt_listbox_get_selected(void *listbox) {
    (void)listbox;
    return NULL;
}

/// @brief Perform listbox get count operation.
/// @param listbox
/// @return Result value.
int64_t rt_listbox_get_count(void *listbox) {
    (void)listbox;
    return 0;
}

/// @brief Perform listbox get selected index operation.
/// @param listbox
/// @return Result value.
int64_t rt_listbox_get_selected_index(void *listbox) {
    (void)listbox;
    return -1;
}

/// @brief Perform listbox select index operation.
/// @param listbox
/// @param index
void rt_listbox_select_index(void *listbox, int64_t index) {
    (void)listbox;
    (void)index;
}

/// @brief Perform listbox was selection changed operation.
/// @param listbox
/// @return Result value.
int64_t rt_listbox_was_selection_changed(void *listbox) {
    (void)listbox;
    return 0;
}

/// @brief Perform listbox item get text operation.
/// @param item
/// @return Result value.
rt_string rt_listbox_item_get_text(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Perform listbox item set text operation.
/// @param item
/// @param text
void rt_listbox_item_set_text(void *item, rt_string text) {
    (void)item;
    (void)text;
}

/// @brief Perform listbox item set data operation.
/// @param item
/// @param data
void rt_listbox_item_set_data(void *item, rt_string data) {
    (void)item;
    (void)data;
}

/// @brief Perform listbox item get data operation.
/// @param item
/// @return Result value.
rt_string rt_listbox_item_get_data(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Perform listbox set font operation.
/// @param listbox
/// @param font
/// @param size
void rt_listbox_set_font(void *listbox, void *font, double size) {
    (void)listbox;
    (void)font;
    (void)size;
}

void *rt_radiogroup_new(void) {
    return NULL;
}

/// @brief Perform radiogroup destroy operation.
/// @param group
void rt_radiogroup_destroy(void *group) {
    (void)group;
}

void *rt_radiobutton_new(void *parent, rt_string text, void *group) {
    (void)parent;
    (void)text;
    (void)group;
    return NULL;
}

/// @brief Perform radiobutton is selected operation.
/// @param radio
/// @return Result value.
int64_t rt_radiobutton_is_selected(void *radio) {
    (void)radio;
    return 0;
}

/// @brief Perform radiobutton set selected operation.
/// @param radio
/// @param selected
void rt_radiobutton_set_selected(void *radio, int64_t selected) {
    (void)radio;
    (void)selected;
}

void *rt_spinner_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Perform spinner set value operation.
/// @param spinner
/// @param value
void rt_spinner_set_value(void *spinner, double value) {
    (void)spinner;
    (void)value;
}

/// @brief Perform spinner get value operation.
/// @param spinner
/// @return Result value.
double rt_spinner_get_value(void *spinner) {
    (void)spinner;
    return 0.0;
}

/// @brief Perform spinner set range operation.
/// @param spinner
/// @param min_val
/// @param max_val
void rt_spinner_set_range(void *spinner, double min_val, double max_val) {
    (void)spinner;
    (void)min_val;
    (void)max_val;
}

/// @brief Perform spinner set step operation.
/// @param spinner
/// @param step
void rt_spinner_set_step(void *spinner, double step) {
    (void)spinner;
    (void)step;
}

/// @brief Perform spinner set decimals operation.
/// @param spinner
/// @param decimals
void rt_spinner_set_decimals(void *spinner, int64_t decimals) {
    (void)spinner;
    (void)decimals;
}

void *rt_image_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Perform image set pixels operation.
/// @param image
/// @param pixels
/// @param width
/// @param height
void rt_image_set_pixels(void *image, void *pixels, int64_t width, int64_t height) {
    (void)image;
    (void)pixels;
    (void)width;
    (void)height;
}

/// @brief Perform image clear operation.
/// @param image
void rt_image_clear(void *image) {
    (void)image;
}

/// @brief Perform image set scale mode operation.
/// @param image
/// @param mode
void rt_image_set_scale_mode(void *image, int64_t mode) {
    (void)image;
    (void)mode;
}

/// @brief Perform image set opacity operation.
/// @param image
/// @param opacity
void rt_image_set_opacity(void *image, double opacity) {
    (void)image;
    (void)opacity;
}

void *rt_floatingpanel_new(void *root) {
    (void)root;
    return NULL;
}

/// @brief Perform floatingpanel set position operation.
/// @param panel
/// @param x
/// @param y
void rt_floatingpanel_set_position(void *panel, double x, double y) {
    (void)panel;
    (void)x;
    (void)y;
}

/// @brief Perform floatingpanel set size operation.
/// @param panel
/// @param w
/// @param h
void rt_floatingpanel_set_size(void *panel, double w, double h) {
    (void)panel;
    (void)w;
    (void)h;
}

/// @brief Perform floatingpanel set visible operation.
/// @param panel
/// @param visible
void rt_floatingpanel_set_visible(void *panel, int64_t visible) {
    (void)panel;
    (void)visible;
}

/// @brief Perform floatingpanel add child operation.
/// @param panel
/// @param child
void rt_floatingpanel_add_child(void *panel, void *child) {
    (void)panel;
    (void)child;
}

#endif /* VIPER_ENABLE_GRAPHICS */
