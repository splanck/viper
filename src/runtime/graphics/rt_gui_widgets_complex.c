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
//   - FloatingPanel children are reparented under the panel widget and rendered
//     during the overlay pass so hit testing and destruction stay tree-based.
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
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

/// @brief Safe-cast a handle to a live TabBar widget, or NULL.
static vg_tabbar_t *rt_tabbar_checked(void *handle) {
    return (vg_tabbar_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_TABBAR);
}

/// @brief Safe-cast a handle to a live SplitPane widget, or NULL.
static vg_splitpane_t *rt_splitpane_checked(void *handle) {
    return (vg_splitpane_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_SPLITPANE);
}

/// @brief Safe-cast a handle to a live CodeEditor widget, or NULL.
static vg_codeeditor_t *rt_codeeditor_checked(void *handle) {
    return (vg_codeeditor_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_CODEEDITOR);
}

/// @brief Safe-cast a handle to a live Dropdown widget, or NULL.
static vg_dropdown_t *rt_dropdown_checked(void *handle) {
    return (vg_dropdown_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_DROPDOWN);
}

/// @brief Safe-cast a handle to a live Slider widget, or NULL.
static vg_slider_t *rt_slider_checked(void *handle) {
    return (vg_slider_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_SLIDER);
}

/// @brief Safe-cast a handle to a live ProgressBar widget, or NULL.
static vg_progressbar_t *rt_progressbar_checked(void *handle) {
    return (vg_progressbar_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_PROGRESS);
}

/// @brief Safe-cast a handle to a live ListBox widget, or NULL.
static vg_listbox_t *rt_listbox_checked(void *handle) {
    return (vg_listbox_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_LISTBOX);
}

/// @brief Safe-cast a handle to a live RadioButton widget, or NULL.
static vg_radiobutton_t *rt_radiobutton_checked(void *handle) {
    return (vg_radiobutton_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_RADIO);
}

/// @brief Safe-cast a handle to a live Spinner widget, or NULL.
static vg_spinner_t *rt_spinner_checked(void *handle) {
    return (vg_spinner_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_SPINNER);
}

/// @brief Safe-cast a handle to a live Image widget, or NULL.
static vg_image_t *rt_image_checked(void *handle) {
    return (vg_image_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_IMAGE);
}

#define RT_RADIOGROUP_MAGIC UINT64_C(0x52474452554E544D)          // "RGDRUNTM"
#define RT_RADIOGROUP_DESTROYED_MAGIC UINT64_C(0x5247444445414444) // "RGDDEAD"

typedef struct rt_radiogroup_data {
    uint64_t magic;
    vg_radiogroup_t *group;
} rt_radiogroup_data_t;

static rt_radiogroup_data_t **s_radiogroup_handles = NULL;
static size_t s_radiogroup_handle_count = 0;
static size_t s_radiogroup_handle_cap = 0;

/// @brief Track a radio-group handle in the process-wide registry.
/// @details The registry lets rt_radiogroup_handle_checked validate opaque
///          handles (radio groups aren't widgets, so the generic widget
///          liveness check doesn't apply). Grows the backing array as needed.
/// @return 1 on success, 0 on allocation failure or NULL input.
static int rt_radiogroup_registry_add(rt_radiogroup_data_t *data) {
    if (!data)
        return 0;
    if (s_radiogroup_handle_count >= s_radiogroup_handle_cap) {
        if (s_radiogroup_handle_cap > SIZE_MAX / 2)
            return 0;
        size_t new_cap = s_radiogroup_handle_cap ? s_radiogroup_handle_cap * 2 : 16;
        if (new_cap > SIZE_MAX / sizeof(*s_radiogroup_handles))
            return 0;
        rt_radiogroup_data_t **new_handles =
            (rt_radiogroup_data_t **)realloc(s_radiogroup_handles,
                                             new_cap * sizeof(*s_radiogroup_handles));
        if (!new_handles)
            return 0;
        s_radiogroup_handles = new_handles;
        s_radiogroup_handle_cap = new_cap;
    }
    s_radiogroup_handles[s_radiogroup_handle_count++] = data;
    return 1;
}

/// @brief Remove a radio-group handle from the registry (swap-with-last).
static void rt_radiogroup_registry_remove(rt_radiogroup_data_t *data) {
    if (!data)
        return;
    for (size_t i = 0; i < s_radiogroup_handle_count; i++) {
        if (s_radiogroup_handles[i] == data) {
            s_radiogroup_handles[i] = s_radiogroup_handles[--s_radiogroup_handle_count];
            return;
        }
    }
}

/// @brief Safe-cast an opaque handle to a live radio-group wrapper.
/// @details Verifies the handle is registered AND its magic tag is intact and
///          its backing vg_radiogroup is non-NULL; returns NULL otherwise.
static rt_radiogroup_data_t *rt_radiogroup_handle_checked(void *handle) {
    if (!handle)
        return NULL;
    for (size_t i = 0; i < s_radiogroup_handle_count; i++) {
        if (s_radiogroup_handles[i] == handle) {
            rt_radiogroup_data_t *data = (rt_radiogroup_data_t *)handle;
            return data->magic == RT_RADIOGROUP_MAGIC && data->group ? data : NULL;
        }
    }
    return NULL;
}

/// @brief Destroy a radio group: unregister, free the vg_radiogroup, and
///        stamp the destroyed-magic so stale handles fail validation.
static void rt_radiogroup_dispose(rt_radiogroup_data_t *data) {
    data = rt_radiogroup_handle_checked(data);
    if (!data)
        return;
    rt_radiogroup_registry_remove(data);
    if (data->group) {
        vg_radiogroup_destroy(data->group);
        data->group = NULL;
    }
    data->magic = RT_RADIOGROUP_DESTROYED_MAGIC;
}

/// @brief GC finalizer trampoline → rt_radiogroup_dispose.
static void rt_radiogroup_finalize(void *handle) {
    rt_radiogroup_dispose((rt_radiogroup_data_t *)handle);
}

//=============================================================================
// TabBar Widget
//=============================================================================

/// @brief Create a new tab bar widget for tabbed navigation.
/// @details Creates a vg_tabbar_t strip that displays clickable tabs. Tabs can
///          be added, removed, activated, and optionally have close buttons.
///          Selection changes are detected via rt_tabbar_was_changed (edge-triggered).
/// @param parent Parent container or app handle.
/// @return Opaque tab bar widget handle, or NULL on failure.
void *rt_tabbar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    vg_tabbar_t *tabbar = vg_tabbar_create(parent_widget);
    if (tabbar)
        rt_gui_apply_default_font((vg_widget_t *)tabbar);
    return tabbar;
}

/// @brief Add a new tab to the tab bar.
/// @details Creates a vg_tab_t with the given title and optional close button.
///          The tab is appended to the bar's tab list. If this is the first tab,
///          it becomes the active tab automatically.
/// @param tabbar   Tab bar widget handle.
/// @param title    Tab title text (runtime string, copied internally).
/// @param closable Non-zero to show a close (X) button on the tab.
/// @return Opaque tab handle for later reference, or NULL on failure.
void *rt_tabbar_add_tab(void *tabbar, rt_string title, int64_t closable) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (!tb)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_t *tab = vg_tabbar_add_tab(tb, ctitle, closable != 0);
    free(ctitle);
    return tab;
}

/// @brief Remove a tab from the tab bar and free its resources.
void rt_tabbar_remove_tab(void *tabbar, void *tab) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (tb && tab) {
        vg_tabbar_remove_tab(tb, (vg_tab_t *)tab);
    }
}

/// @brief Set the currently active (selected) tab in the tab bar.
void rt_tabbar_set_active(void *tabbar, void *tab) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (tb) {
        vg_tabbar_set_active(tb, (vg_tab_t *)tab);
    }
}

/// @brief Update the title text of a tab.
void rt_tab_set_title(void *tab, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    if (!vg_tab_is_live((vg_tab_t *)tab))
        return;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_set_title((vg_tab_t *)tab, ctitle);
    free(ctitle);
}

/// @brief Update the tooltip text of a tab.
void rt_tab_set_tooltip(void *tab, rt_string tooltip) {
    RT_ASSERT_MAIN_THREAD();
    if (!vg_tab_is_live((vg_tab_t *)tab))
        return;
    char *ctooltip = rt_string_to_cstr(tooltip);
    vg_tab_set_tooltip((vg_tab_t *)tab, ctooltip);
    free(ctooltip);
}

/// @brief Mark a tab as modified (shows an unsaved-changes indicator).
void rt_tab_set_modified(void *tab, int64_t modified) {
    RT_ASSERT_MAIN_THREAD();
    if (vg_tab_is_live((vg_tab_t *)tab)) {
        vg_tab_set_modified((vg_tab_t *)tab, modified != 0);
    }
}

/// @brief Return the currently-active tab handle (NULL when no tabs / null bar).
void *rt_tabbar_get_active(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    return tb ? tb->active_tab : NULL;
}

/// @brief Get the active index of the tabbar.
int64_t rt_tabbar_get_active_index(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (!tb)
        return -1;
    return vg_tabbar_get_tab_index(tb, tb->active_tab);
}

/// @brief Check if the active tab changed since the last call (edge-triggered).
int64_t rt_tabbar_was_changed(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (!tb)
        return 0;
    if (tb->reported_active_change_version != tb->active_change_version) {
        tb->reported_active_change_version = tb->active_change_version;
        return 1;
    }
    return 0;
}

/// @brief Get the number of tabs in the tab bar.
int64_t rt_tabbar_get_tab_count(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    return tb ? tb->tab_count : 0;
}

/// @brief Check if any tab's close button was clicked this frame.
int64_t rt_tabbar_was_close_clicked(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (!tb)
        return 0;
    return tb->close_clicked_index >= 0 ? 1 : 0;
}

/// @brief Get the index of the tab whose close button was clicked (clears after read).
int64_t rt_tabbar_get_close_clicked_index(void *tabbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (!tb)
        return -1;
    if (tb->close_clicked_index < 0)
        return -1;
    int index = tb->close_clicked_index;
    tb->close_clicked_index = -1;
    return index;
}

/// @brief Return the tab at position `index`, or NULL if out of range.
void *rt_tabbar_get_tab_at(void *tabbar, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (!tb)
        return NULL;
    if (index < 0 || index > INT_MAX || index >= (int64_t)tb->tab_count)
        return NULL;
    return vg_tabbar_get_tab_at(tb, (int)index);
}

/// @brief Enable or disable automatic tab removal on close-button click.
void rt_tabbar_set_auto_close(void *tabbar, int64_t auto_close) {
    RT_ASSERT_MAIN_THREAD();
    vg_tabbar_t *tb = rt_tabbar_checked(tabbar);
    if (tb) {
        tb->auto_close = auto_close != 0;
    }
}

//=============================================================================
// SplitPane Widget
//=============================================================================

/// @brief Create a new resizable split pane widget.
/// @details Creates a vg_splitpane_t that divides its area into two panels
///          separated by a draggable divider. The panels are accessible via
///          rt_splitpane_get_first/get_second; add children to those containers.
/// @param parent     Parent container or app handle.
/// @param horizontal Non-zero for a horizontal split (left|right), zero for vertical (top|bottom).
/// @return Opaque split pane widget handle, or NULL on failure.
void *rt_splitpane_new(void *parent, int64_t horizontal) {
    RT_ASSERT_MAIN_THREAD();
    vg_split_direction_t direction = horizontal ? VG_SPLIT_HORIZONTAL : VG_SPLIT_VERTICAL;
    return vg_splitpane_create(rt_gui_widget_parent_from_handle(parent), direction);
}

/// @brief Set the divider position as a fraction of the split pane's size.
void rt_splitpane_set_position(void *split, double position) {
    RT_ASSERT_MAIN_THREAD();
    vg_splitpane_t *sp = rt_splitpane_checked(split);
    if (sp) {
        vg_splitpane_set_position(sp,
                                  (float)rt_gui_clamp_f64(
                                      rt_gui_double_is_finite(position) ? position : 0.5, 0.0, 1.0));
    }
}

// BINDING-006: SplitPane position query
/// @brief Get the position of the splitpane.
double rt_splitpane_get_position(void *split) {
    RT_ASSERT_MAIN_THREAD();
    vg_splitpane_t *sp = rt_splitpane_checked(split);
    if (!sp)
        return 0.5;
    return (double)vg_splitpane_get_position(sp);
}

/// @brief Return the first (left/top) panel container of a split pane.
/// Add child widgets to this container to populate the leading half.
void *rt_splitpane_get_first(void *split) {
    RT_ASSERT_MAIN_THREAD();
    vg_splitpane_t *sp = rt_splitpane_checked(split);
    if (!sp)
        return NULL;
    return vg_splitpane_get_first(sp);
}

/// @brief Return the second (right/bottom) panel container of a split pane.
void *rt_splitpane_get_second(void *split) {
    RT_ASSERT_MAIN_THREAD();
    vg_splitpane_t *sp = rt_splitpane_checked(split);
    if (!sp)
        return NULL;
    return vg_splitpane_get_second(sp);
}

//=============================================================================
// CodeEditor Widget
//=============================================================================

/// @brief Create a new source code editor widget.
/// @details Creates a full-featured vg_codeeditor_t with line numbers, syntax
///          highlighting, text selection, clipboard support, and undo/redo.
///          Designed for displaying and editing source code in IDE-style UIs.
/// @param parent Parent container or app handle.
/// @return Opaque code editor widget handle, or NULL on failure.
void *rt_codeeditor_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    vg_codeeditor_t *editor = vg_codeeditor_create(parent_widget);
    if (editor)
        rt_gui_apply_default_font((vg_widget_t *)editor);
    return editor;
}

/// @brief Replace the entire text content of a code editor.
void rt_codeeditor_set_text(void *editor, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (!ce)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_codeeditor_set_text(ce, ctext);
    free(ctext);
}

/// @brief Retrieve the full text content of a code editor (caller frees the C string).
rt_string rt_codeeditor_get_text(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (!ce)
        return rt_str_empty();
    char *text = vg_codeeditor_get_text(ce);
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

/// @brief Retrieve the currently selected text in a code editor.
rt_string rt_codeeditor_get_selected_text(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (!ce)
        return rt_str_empty();
    char *text = vg_codeeditor_get_selection(ce);
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

/// @brief Move the cursor to a specific line and column in the code editor.
void rt_codeeditor_set_cursor(void *editor, int64_t line, int64_t col) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (ce) {
        vg_codeeditor_set_cursor(ce,
                                 rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX),
                                 rt_gui_clamp_i64_to_i32(col, 0, INT32_MAX));
    }
}

/// @brief Scroll the code editor viewport to make a specific line visible.
void rt_codeeditor_scroll_to_line(void *editor, int64_t line) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (ce) {
        vg_codeeditor_scroll_to_line(ce,
                                     rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX));
    }
}

/// @brief Get the total number of lines in the code editor.
int64_t rt_codeeditor_get_line_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (!ce)
        return 0;
    return vg_codeeditor_get_line_count(ce);
}

/// @brief Check whether the code editor's content has been modified since last clear.
int64_t rt_codeeditor_is_modified(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (!ce)
        return 0;
    return vg_codeeditor_is_modified(ce) ? 1 : 0;
}

/// @brief Reset the code editor's modified flag (e.g., after saving).
void rt_codeeditor_clear_modified(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (ce) {
        vg_codeeditor_clear_modified(ce);
    }
}

/// @brief Set the font of the codeeditor.
void rt_codeeditor_set_font(void *editor, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_checked(editor);
    if (ce) {
        rt_gui_app_t *app = rt_gui_app_from_widget((vg_widget_t *)ce);
        float _s = (app && app->window) ? vgfx_window_get_scale(app->window) : 1.0f;
        if (!isfinite(_s) || _s <= 0.0f)
            _s = 1.0f;
        vg_codeeditor_set_font(ce,
                               (vg_font_t *)font,
                               (float)rt_gui_sanitize_font_size(size, 14.0) * _s);
    }
}

/// @brief Get or set the font size of the code editor (in logical points).
double rt_codeeditor_get_font_size(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ed = rt_codeeditor_checked(editor);
    if (!ed)
        return 14.0;
    rt_gui_app_t *app = rt_gui_app_from_widget((vg_widget_t *)ed);
    // Return logical pt size — divide stored physical pixels by HiDPI scale.
    float _s = (app && app->window) ? vgfx_window_get_scale(app->window) : 1.0f;
    if (!isfinite(_s) || _s <= 0.0f)
        _s = 1.0f;
    return (double)(ed->font_size / _s);
}

/// @brief Get or set the font size of the code editor (in logical points).
void rt_codeeditor_set_font_size(void *editor, double size) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ed = rt_codeeditor_checked(editor);
    if (!ed)
        return;
    if (rt_gui_double_is_finite(size) && size > 0.0) {
        rt_gui_app_t *app = rt_gui_app_from_widget((vg_widget_t *)ed);
        // Store physical pixels — multiply logical pt size by HiDPI scale.
        float _s = (app && app->window) ? vgfx_window_get_scale(app->window) : 1.0f;
        if (!isfinite(_s) || _s <= 0.0f)
            _s = 1.0f;
        vg_codeeditor_set_font(ed, ed->font, (float)rt_gui_sanitize_font_size(size, 14.0) * _s);
    }
}

//=============================================================================
// Theme Functions
//=============================================================================

/// @brief Recompute the theme's HiDPI-scaled dimensions from the active window.
void rt_theme_apply_hidpi_scale(void) {
    rt_gui_refresh_theme(rt_gui_get_active_app());
}

/// @brief Switch the active theme to dark mode.
void rt_theme_set_dark(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    if (!app) {
        vg_theme_set_current(vg_theme_dark());
        return;
    }
    rt_gui_set_theme_kind(app, RT_GUI_THEME_DARK);
}

/// @brief Switch the active theme to light mode.
void rt_theme_set_light(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    if (!app) {
        vg_theme_set_current(vg_theme_light());
        return;
    }
    rt_gui_set_theme_kind(app, RT_GUI_THEME_LIGHT);
}

/// @brief Get the name of the theme.
rt_string rt_theme_get_name(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    const char *name = "dark";
    if (app) {
        name = (app->theme_kind == RT_GUI_THEME_LIGHT) ? "light" : "dark";
    } else {
        vg_theme_t *current = vg_theme_get_current();
        if (current && current->name && strcasecmp(current->name, "Light") == 0)
            name = "light";
    }
    return rt_string_from_bytes(name, strlen(name));
}

//=============================================================================
// Layout Functions
//=============================================================================

/// @brief Create a vertical box-layout container (children stacked top-to-bottom).
void *rt_vbox_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_vbox_create(0.0f);
}

/// @brief Create a horizontal box-layout container (children laid out left-to-right).
void *rt_hbox_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_hbox_create(0.0f);
}

/// @brief Set the spacing of the container.
void rt_container_set_spacing(void *container, double spacing) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_gui_widget_handle_checked(container);
    if (!widget)
        return;
    vg_container_set_spacing(widget,
                             rt_gui_sanitize_nonnegative_float(spacing, RT_GUI_MAX_LAYOUT_VALUE));
}

/// @brief Set the padding of the container.
void rt_container_set_padding(void *container, double padding) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_gui_widget_handle_checked(container);
    if (widget) {
        vg_widget_set_padding(
            widget,
            rt_gui_sanitize_nonnegative_float(padding, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

//=============================================================================
// Widget State Functions
//=============================================================================

/// @brief Check whether the mouse cursor is currently over this widget.
int64_t rt_widget_is_hovered(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *w = rt_gui_widget_handle_checked(widget);
    if (!w)
        return 0;
    return (w->state & VG_STATE_HOVERED) ? 1 : 0;
}

/// @brief Check whether the widget is currently being pressed (mouse down).
int64_t rt_widget_is_pressed(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *w = rt_gui_widget_handle_checked(widget);
    if (!w)
        return 0;
    return (w->state & VG_STATE_PRESSED) ? 1 : 0;
}

/// @brief Check whether the widget currently has keyboard focus.
int64_t rt_widget_is_focused(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *w = rt_gui_widget_handle_checked(widget);
    if (!w)
        return 0;
    return (w->state & VG_STATE_FOCUSED) ? 1 : 0;
}

/// @brief Move keyboard focus to a widget that participates in the tab order.
void rt_widget_focus(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *w = rt_gui_widget_handle_checked(widget);
    if (!w)
        return;
    vg_widget_set_focus(w);
}

/// @brief Set the last clicked value.
/// @param widget
void rt_gui_set_last_clicked(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *w = rt_gui_widget_handle_checked(widget);
    if (widget && !w)
        return;
    rt_gui_app_t *app = w ? rt_gui_app_from_widget(w) : rt_gui_get_active_app();
    if (app)
        app->last_clicked = w;
}

/// @brief Check whether this widget was clicked during the current frame.
int64_t rt_widget_was_clicked(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *w = rt_gui_widget_handle_checked(widget);
    if (!w)
        return 0;
    rt_gui_app_t *app = rt_gui_app_from_widget(w);
    return (app && app->last_clicked == w) ? 1 : 0;
}

/// @brief Set the position of the widget.
/// @details Intended for widgets that are manually positioned outside managed
///          layout containers. Managed layouts may override x/y on the next
///          layout pass.
void rt_widget_set_position(void *widget, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *w = rt_gui_widget_handle_checked(widget);
    if (w) {
        w->x = rt_gui_sanitize_signed_float((double)x, RT_GUI_MAX_LAYOUT_VALUE);
        w->y = rt_gui_sanitize_signed_float((double)y, RT_GUI_MAX_LAYOUT_VALUE);
        vg_widget_invalidate_layout(w);
        vg_widget_invalidate(w);
    }
}

//=============================================================================
// Dropdown Widget
//=============================================================================

/// @brief Create a dropdown (combo box) widget — a button that pops a list of choices.
void *rt_dropdown_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dropdown = vg_dropdown_create(rt_gui_widget_parent_from_handle(parent));
    rt_gui_apply_default_font((vg_widget_t *)dropdown);
    return dropdown;
}

/// @brief Add a selectable item to a dropdown list.
int64_t rt_dropdown_add_item(void *dropdown, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return -1;
    char *ctext = rt_string_to_cstr(text);
    int64_t index = vg_dropdown_add_item(dd, ctext);
    free(ctext);
    return index;
}

/// @brief Remove an item from a dropdown by index.
void rt_dropdown_remove_item(void *dropdown, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (dd) {
        vg_dropdown_remove_item(dd,
                                rt_gui_clamp_i64_to_i32(index, INT32_MIN, INT32_MAX));
    }
}

/// @brief Remove all items from a dropdown, leaving it empty.
void rt_dropdown_clear(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (dd) {
        vg_dropdown_clear(dd);
    }
}

/// @brief Programmatically select a dropdown item by index.
void rt_dropdown_set_selected(void *dropdown, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (dd) {
        vg_dropdown_set_selected(dd,
                                 rt_gui_clamp_i64_to_i32(index, INT32_MIN, INT32_MAX));
    }
}

/// @brief Get the index of the currently selected dropdown item (-1 if none).
int64_t rt_dropdown_get_selected(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return -1;
    return vg_dropdown_get_selected(dd);
}

/// @brief Get the selected text of the dropdown.
rt_string rt_dropdown_get_selected_text(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return rt_str_empty();
    const char *text = vg_dropdown_get_selected_text(dd);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

/// @brief Set the placeholder of the dropdown.
void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_dropdown_set_placeholder(dd, ctext);
    free(ctext);
}

//=============================================================================
// Slider Widget
//=============================================================================

/// @brief Create a slider widget for picking a numeric value within a range.
/// @param horizontal Non-zero for left-right slider, zero for top-bottom.
void *rt_slider_new(void *parent, int64_t horizontal) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_orientation_t orient = horizontal ? VG_SLIDER_HORIZONTAL : VG_SLIDER_VERTICAL;
    return vg_slider_create(rt_gui_widget_parent_from_handle(parent), orient);
}

/// @brief Set the value of the slider.
void rt_slider_set_value(void *slider, double value) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (sl) {
        if (!rt_gui_double_is_finite(value))
            return;
        vg_slider_set_value(sl,
                            rt_gui_sanitize_signed_float(value, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

/// @brief Get the value of the slider.
double rt_slider_get_value(void *slider) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (!sl)
        return 0.0;
    return (double)vg_slider_get_value(sl);
}

/// @brief Set the range of the slider.
void rt_slider_set_range(void *slider, double min_val, double max_val) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (sl) {
        if (!rt_gui_double_is_finite(min_val) || !rt_gui_double_is_finite(max_val))
            return;
        if (min_val > max_val) {
            double tmp = min_val;
            min_val = max_val;
            max_val = tmp;
        }
        vg_slider_set_range(sl,
                            rt_gui_sanitize_signed_float(min_val, RT_GUI_MAX_LAYOUT_VALUE),
                            rt_gui_sanitize_signed_float(max_val, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

/// @brief Set the step of the slider.
void rt_slider_set_step(void *slider, double step) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (sl) {
        vg_slider_set_step(sl,
                           rt_gui_sanitize_nonnegative_float(step, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

//=============================================================================
// ProgressBar Widget
//=============================================================================

/// @brief Create a horizontal progress bar (0.0–1.0 fill range).
void *rt_progressbar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = vg_progressbar_create(rt_gui_widget_parent_from_handle(parent));
    if (pb)
        rt_gui_apply_default_font((vg_widget_t *)pb);
    return pb;
}

/// @brief Set the value of the progressbar.
void rt_progressbar_set_value(void *progress, double value) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (pb) {
        double sanitized = rt_gui_double_is_finite(value) ? value : 0.0;
        vg_progressbar_set_value(pb,
                                 (float)rt_gui_clamp_f64(sanitized, 0.0, 1.0));
    }
}

/// @brief Get the value of the progressbar.
double rt_progressbar_get_value(void *progress) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (!pb)
        return 0.0;
    return (double)vg_progressbar_get_value(pb);
}

/// @brief Set the visual style of a progress bar (bar, circle, or indeterminate).
/// @details Controls how progress is rendered: VG_PROGRESS_BAR for a standard
///          horizontal fill, VG_PROGRESS_INDETERMINATE for a looping animation
///          when the total duration is unknown. Out-of-range values default to bar.
/// @param progress Progress bar widget handle.
/// @param style    Style enum value (VG_PROGRESS_BAR, VG_PROGRESS_INDETERMINATE, etc.).
void rt_progressbar_set_style(void *progress, int64_t style) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (!pb)
        return;
    if (style < (int64_t)VG_PROGRESS_BAR || style > (int64_t)VG_PROGRESS_INDETERMINATE)
        style = (int64_t)VG_PROGRESS_BAR;
    vg_progressbar_set_style(pb, (vg_progress_style_t)style);
}

/// @brief Show or hide the percentage text label on a progress bar.
/// @param progress Progress bar widget handle.
/// @param show     Non-zero to render the "XX%" label, zero to hide it.
void rt_progressbar_show_percentage(void *progress, int64_t show) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (pb)
        vg_progressbar_show_percentage(pb, show != 0);
}

//=============================================================================
// ListBox Widget
//=============================================================================

/// @brief Create an empty scrollable list-box widget.
void *rt_listbox_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *listbox = vg_listbox_create(rt_gui_widget_parent_from_handle(parent));
    rt_gui_apply_default_font((vg_widget_t *)listbox);
    return listbox;
}

/// @brief Append `text` as a new list-box item; returns the new item handle.
void *rt_listbox_add_item(void *listbox, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_listbox_item_t *item = vg_listbox_add_item(lb, ctext, NULL);
    free(ctext);
    return item;
}

/// @brief Remove an item from the listbox.
void rt_listbox_remove_item(void *listbox, void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb && item) {
        vg_listbox_remove_item(lb, (vg_listbox_item_t *)item);
    }
}

/// @brief Remove all entries from the listbox.
void rt_listbox_clear(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb) {
        vg_listbox_clear(lb);
    }
}

/// @brief Programmatically select a listbox item by handle.
void rt_listbox_select(void *listbox, void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb) {
        vg_listbox_select(lb, (vg_listbox_item_t *)item);
    }
}

/// @brief Return the currently-selected listbox item handle (NULL when none).
void *rt_listbox_get_selected(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return NULL;
    return vg_listbox_get_selected(lb);
}

/// @brief Get the number of items in the listbox.
int64_t rt_listbox_get_count(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return 0;
    return (int64_t)lb->item_count;
}

/// @brief Get the selected index of the listbox.
int64_t rt_listbox_get_selected_index(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return -1;
    size_t idx = vg_listbox_get_selected_index(lb);
    if (idx == (size_t)-1)
        return -1;
    return (int64_t)idx;
}

/// @brief Select a listbox item by its zero-based index.
void rt_listbox_select_index(void *listbox, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb || index < 0)
        return;
    vg_listbox_select_index(lb, (size_t)index);
}

/// @brief Check if the listbox selection changed since the last call (edge-triggered).
int64_t rt_listbox_was_selection_changed(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return 0;

    if (lb->virtual_mode) {
        if (lb->selected_index != lb->prev_selected_index) {
            lb->prev_selected_index = lb->selected_index;
            return 1;
        }
        return 0;
    }

    if (lb->selected != lb->prev_selected) {
        lb->prev_selected = lb->selected;
        return 1;
    }
    return 0;
}

/// @brief Get the display text of a listbox item.
rt_string rt_listbox_item_get_text(void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (!vg_listbox_item_is_live(it))
        return rt_str_empty();
    if (it->text)
        return rt_string_from_bytes(it->text, strlen(it->text));
    return rt_str_empty();
}

/// @brief Update the display text of a listbox item.
void rt_listbox_item_set_text(void *item, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (!vg_listbox_item_is_live(it))
        return;
    char *ctext = rt_string_to_cstr(text);
    if (!ctext)
        return;
    free(it->text);
    it->text = ctext; // Takes ownership
    if (it->owner) {
        it->owner->base.needs_layout = true;
        it->owner->base.needs_paint = true;
    }
}

/// @brief Attach arbitrary string data to a listbox item (replaces previous data).
void rt_listbox_item_set_data(void *item, rt_string data) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (!vg_listbox_item_is_live(it))
        return;
    rt_gui_string_data_t *new_data = data ? rt_gui_string_data_new(data) : NULL;
    if (data && !new_data)
        return;
    if (it->owns_user_data && it->user_data)
        free(it->user_data);
    it->user_data = new_data;
    it->owns_user_data = new_data != NULL;
}

/// @brief Retrieve the string data previously attached to a listbox item.
rt_string rt_listbox_item_get_data(void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    vg_listbox_item_t *it = (vg_listbox_item_t *)item;
    if (!vg_listbox_item_is_live(it))
        return rt_str_empty();
    if (!it->user_data || !it->owns_user_data)
        return rt_str_empty();
    return rt_gui_string_data_to_rt_string(it->user_data);
}

/// @brief Set the font of the listbox.
void rt_listbox_set_font(void *listbox, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb) {
        vg_listbox_set_font(lb,
                            (vg_font_t *)font,
                            (float)rt_gui_sanitize_font_size(size, 14.0));
    }
}

//=============================================================================
// RadioButton Widget
//=============================================================================

/// @brief Create a radio-button group — only one member may be selected at a time.
void *rt_radiogroup_new(void) {
    RT_ASSERT_MAIN_THREAD();
    vg_radiogroup_t *group = vg_radiogroup_create();
    if (!group)
        return NULL;
    rt_radiogroup_data_t *data =
        (rt_radiogroup_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_radiogroup_data_t));
    if (!data) {
        vg_radiogroup_destroy(group);
        return NULL;
    }
    data->magic = RT_RADIOGROUP_MAGIC;
    data->group = group;
    if (!rt_radiogroup_registry_add(data)) {
        vg_radiogroup_destroy(group);
        data->magic = RT_RADIOGROUP_DESTROYED_MAGIC;
        data->group = NULL;
        return NULL;
    }
    rt_obj_set_finalizer(data, rt_radiogroup_finalize);
    return data;
}

/// @brief Release resources and destroy the radiogroup.
void rt_radiogroup_destroy(void *group) {
    RT_ASSERT_MAIN_THREAD();
    rt_radiogroup_dispose((rt_radiogroup_data_t *)group);
}

/// @brief Create a single radio button bound to a given group.
/// Selecting one radio in the group automatically deselects the others.
void *rt_radiobutton_new(void *parent, rt_string text, void *group) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    rt_radiogroup_data_t *group_data = NULL;
    if (group) {
        group_data = rt_radiogroup_handle_checked(group);
        if (!group_data)
            return NULL;
    }
    char *ctext = rt_string_to_cstr(text);
    vg_radiobutton_t *radio =
        vg_radiobutton_create(parent_widget, ctext, group_data ? group_data->group : NULL);
    free(ctext);
    if (radio)
        rt_gui_apply_default_font((vg_widget_t *)radio);
    return radio;
}

/// @brief Check whether a radio button is currently selected in its group.
int64_t rt_radiobutton_is_selected(void *radio) {
    RT_ASSERT_MAIN_THREAD();
    vg_radiobutton_t *rb = rt_radiobutton_checked(radio);
    if (!rb)
        return 0;
    return vg_radiobutton_is_selected(rb) ? 1 : 0;
}

/// @brief Programmatically select a radio button (deselects siblings in the group).
void rt_radiobutton_set_selected(void *radio, int64_t selected) {
    RT_ASSERT_MAIN_THREAD();
    vg_radiobutton_t *rb = rt_radiobutton_checked(radio);
    if (rb) {
        vg_radiobutton_set_selected(rb, selected != 0);
    }
}

//=============================================================================
// Spinner Widget
//=============================================================================

/// @brief Create a numeric spinner widget (text field with up/down increment buttons).
void *rt_spinner_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_spinner_t *spinner = vg_spinner_create(rt_gui_widget_parent_from_handle(parent));
    if (spinner)
        rt_gui_apply_default_font((vg_widget_t *)spinner);
    return spinner;
}

/// @brief Set the value of the spinner.
void rt_spinner_set_value(void *spinner, double value) {
    RT_ASSERT_MAIN_THREAD();
    vg_spinner_t *sp = rt_spinner_checked(spinner);
    if (sp) {
        if (!rt_gui_double_is_finite(value))
            return;
        vg_spinner_set_value(sp,
                             rt_gui_clamp_f64(
                                 value, -RT_GUI_MAX_LAYOUT_VALUE, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

/// @brief Get the value of the spinner.
double rt_spinner_get_value(void *spinner) {
    RT_ASSERT_MAIN_THREAD();
    vg_spinner_t *sp = rt_spinner_checked(spinner);
    if (!sp)
        return 0.0;
    return vg_spinner_get_value(sp);
}

/// @brief Set the range of the spinner.
void rt_spinner_set_range(void *spinner, double min_val, double max_val) {
    RT_ASSERT_MAIN_THREAD();
    vg_spinner_t *sp = rt_spinner_checked(spinner);
    if (sp) {
        if (!rt_gui_double_is_finite(min_val) || !rt_gui_double_is_finite(max_val))
            return;
        if (min_val > max_val) {
            double tmp = min_val;
            min_val = max_val;
            max_val = tmp;
        }
        min_val = rt_gui_clamp_f64(min_val, -RT_GUI_MAX_LAYOUT_VALUE, RT_GUI_MAX_LAYOUT_VALUE);
        max_val = rt_gui_clamp_f64(max_val, -RT_GUI_MAX_LAYOUT_VALUE, RT_GUI_MAX_LAYOUT_VALUE);
        vg_spinner_set_range(sp, min_val, max_val);
    }
}

/// @brief Set the step of the spinner.
void rt_spinner_set_step(void *spinner, double step) {
    RT_ASSERT_MAIN_THREAD();
    vg_spinner_t *sp = rt_spinner_checked(spinner);
    if (sp) {
        vg_spinner_set_step(sp,
                            (double)rt_gui_sanitize_nonnegative_float(step, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

/// @brief Set the decimals of the spinner.
void rt_spinner_set_decimals(void *spinner, int64_t decimals) {
    RT_ASSERT_MAIN_THREAD();
    vg_spinner_t *sp = rt_spinner_checked(spinner);
    if (sp) {
        vg_spinner_set_decimals(sp,
                                rt_gui_clamp_i64_to_i32(decimals, 0, 9));
    }
}

//=============================================================================
// Image Widget
//=============================================================================

/// @brief Create an image widget — displays a Pixels object as a static image.
void *rt_image_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    return vg_image_create(rt_gui_widget_parent_from_handle(parent));
}

/// @brief Convert a Viper Pixels object to byte-order RGBA and upload to an image widget.
/// @details Viper stores pixels as packed 0xRRGGBBAA 32-bit integers; vg_image_set_pixels
///          expects interleaved [R, G, B, A] bytes. This function allocates a temporary
///          RGBA buffer, shuffles channels per-pixel, calls vg_image_set_pixels, then
///          frees the buffer. width/height 0 defaults to the Pixels object dimensions.
/// @param image  Image widget to update (may be NULL — no-op).
/// @param pixels Viper Pixels object providing source pixel data (NULL clears the image).
/// @param width  Crop width (0 = full Pixels width).
/// @param height Crop height (0 = full Pixels height).
/// @return 1 on success, 0 on failure (NULL image, zero dimensions, or OOM).
static int rt_image_set_from_pixels_object(vg_image_t *image,
                                           void *pixels,
                                           int64_t width,
                                           int64_t height) {
    if (!image)
        return 0;
    if (!pixels) {
        vg_image_clear(image);
        return 1;
    }

    int64_t source_w = rt_pixels_width(pixels);
    int64_t source_h = rt_pixels_height(pixels);
    const uint32_t *src = rt_pixels_raw_buffer(pixels);
    if (source_w <= 0 || source_h <= 0 || !src)
        return 0;
    if ((size_t)source_w > SIZE_MAX / (size_t)source_h)
        return 0;

    if (width <= 0)
        width = source_w;
    if (height <= 0)
        height = source_h;
    if (width > source_w)
        width = source_w;
    if (height > source_h)
        height = source_h;
    if (width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX)
        return 0;

    size_t w = (size_t)width;
    size_t h = (size_t)height;
    if (w > SIZE_MAX / h || w * h > SIZE_MAX / 4)
        return 0;

    size_t pixel_count = w * h;
    uint8_t *rgba = (uint8_t *)malloc(pixel_count * 4);
    if (!rgba)
        return 0;

    size_t stride = (size_t)source_w;
    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            uint32_t px = src[y * stride + x];
            size_t out = (y * w + x) * 4;
            rgba[out + 0] = (uint8_t)((px >> 24) & 0xFF);
            rgba[out + 1] = (uint8_t)((px >> 16) & 0xFF);
            rgba[out + 2] = (uint8_t)((px >> 8) & 0xFF);
            rgba[out + 3] = (uint8_t)(px & 0xFF);
        }
    }

    vg_image_set_pixels(image, rgba, (int)width, (int)height);
    free(rgba);
    return 1;
}

/// @brief Set the pixels of the image.
void rt_image_set_pixels(void *image, void *pixels, int64_t width, int64_t height) {
    RT_ASSERT_MAIN_THREAD();
    rt_image_set_from_pixels_object(rt_image_checked(image), pixels, width, height);
}

/// @brief Clear the image widget's pixel data, showing nothing.
void rt_image_clear(void *image) {
    RT_ASSERT_MAIN_THREAD();
    vg_image_t *img = rt_image_checked(image);
    if (img) {
        vg_image_clear(img);
    }
}

/// @brief Set the scale mode of the image.
void rt_image_set_scale_mode(void *image, int64_t mode) {
    RT_ASSERT_MAIN_THREAD();
    vg_image_t *img = rt_image_checked(image);
    if (img) {
        vg_image_set_scale_mode(img,
                                (vg_image_scale_t)rt_gui_clamp_i64_to_i32(mode, 0, 3));
    }
}

/// @brief Set the opacity of the image.
void rt_image_set_opacity(void *image, double opacity) {
    RT_ASSERT_MAIN_THREAD();
    vg_image_t *img = rt_image_checked(image);
    if (img) {
        double sanitized = rt_gui_double_is_finite(opacity) ? opacity : 1.0;
        vg_image_set_opacity(img,
                             (float)rt_gui_clamp_f64(sanitized, 0.0, 1.0));
    }
}

/// @brief Load an image file (PNG, BMP, JPEG, or GIF) into the image widget.
/// @details Auto-detects format from file magic bytes, decodes using rt_pixels,
///          converts from packed 0xRRGGBBAA to byte RGBA, and sets the widget pixels.
/// @param image Image widget.
/// @param path File path (runtime string).
/// @return 1 on success, 0 on failure.
int64_t rt_image_load_file(void *image, void *path) {
    RT_ASSERT_MAIN_THREAD();
    vg_image_t *img = rt_image_checked(image);
    if (!img || !path)
        return 0;

    // Try PNG first, then BMP, then JPEG, then GIF
    void *pixels = rt_pixels_load_png(path);
    if (!pixels)
        pixels = rt_pixels_load_bmp(path);
    if (!pixels)
        pixels = rt_pixels_load_jpeg(path);
    if (!pixels)
        pixels = rt_pixels_load_gif(path);
    if (!pixels)
        return 0;

    int ok = rt_image_set_from_pixels_object(img, pixels, 0, 0);
    if (rt_obj_release_check0(pixels))
        rt_obj_free(pixels);
    return ok ? 1 : 0;
}

//=============================================================================
// FloatingPanel Widget
//=============================================================================

/// @brief Create a free-floating panel — a draggable container that floats above its parent.
///
/// Used for tool palettes, inspectors, and side panels that the
/// user can reposition. `root` is the top-level app handle that
/// owns the panel's draw layer.
void *rt_floatingpanel_new(void *root) {
    RT_ASSERT_MAIN_THREAD();
    return vg_floatingpanel_create(rt_gui_widget_parent_from_handle(root));
}

static vg_floatingpanel_t *rt_floatingpanel_checked(void *panel) {
    vg_floatingpanel_t *fp = (vg_floatingpanel_t *)panel;
    return vg_floatingpanel_is_live(fp) ? fp : NULL;
}

/// @brief Set the position of the floatingpanel.
void rt_floatingpanel_set_position(void *panel, double x, double y) {
    RT_ASSERT_MAIN_THREAD();
    vg_floatingpanel_t *fp = rt_floatingpanel_checked(panel);
    if (fp)
        vg_floatingpanel_set_position(
            fp,
            rt_gui_sanitize_signed_float(x, RT_GUI_MAX_LAYOUT_VALUE),
            rt_gui_sanitize_signed_float(y, RT_GUI_MAX_LAYOUT_VALUE));
}

/// @brief Set the width and height of a floating panel.
void rt_floatingpanel_set_size(void *panel, double w, double h) {
    RT_ASSERT_MAIN_THREAD();
    vg_floatingpanel_t *fp = rt_floatingpanel_checked(panel);
    if (fp)
        vg_floatingpanel_set_size(
            fp,
            rt_gui_sanitize_nonnegative_float(w, RT_GUI_MAX_LAYOUT_VALUE),
            rt_gui_sanitize_nonnegative_float(h, RT_GUI_MAX_LAYOUT_VALUE));
}

/// @brief Show or hide a floating panel overlay.
void rt_floatingpanel_set_visible(void *panel, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    vg_floatingpanel_t *fp = rt_floatingpanel_checked(panel);
    if (fp)
        vg_floatingpanel_set_visible(fp, visible != 0);
}

/// @brief Add a child widget to a floating panel's content area.
void rt_floatingpanel_add_child(void *panel, void *child) {
    RT_ASSERT_MAIN_THREAD();
    vg_floatingpanel_t *fp = rt_floatingpanel_checked(panel);
    vg_widget_t *child_widget = rt_gui_widget_handle_checked(child);
    if (fp && child_widget)
        vg_floatingpanel_add_child(fp, child_widget);
}

#else /* !VIPER_ENABLE_GRAPHICS */

// ===========================================================================
// Headless stubs — same prototypes as the real implementations above so
// non-graphical builds (server / CLI / ViperDOS) can link without pulling
// in the GUI subsystem. Each stub safely no-ops or returns a sentinel
// (NULL pointer, 0, -1, or empty string). Doc comments are inherited
// from the real implementations above by virtue of identical names.
// ===========================================================================

/// @brief Stub: graphics disabled — returns NULL; no tab bar widget is created.
void *rt_tabbar_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no tab is added.
void *rt_tabbar_add_tab(void *tabbar, rt_string title, int64_t closable) {
    (void)tabbar;
    (void)title;
    (void)closable;
    return NULL;
}

/// @brief Remove a tab from the tab bar and free its resources.
void rt_tabbar_remove_tab(void *tabbar, void *tab) {
    (void)tabbar;
    (void)tab;
}

/// @brief Set the currently active (selected) tab in the tab bar.
void rt_tabbar_set_active(void *tabbar, void *tab) {
    (void)tabbar;
    (void)tab;
}

/// @brief Update the title text of a tab.
void rt_tab_set_title(void *tab, rt_string title) {
    (void)tab;
    (void)title;
}

/// @brief Update the tooltip text of a tab.
void rt_tab_set_tooltip(void *tab, rt_string tooltip) {
    (void)tab;
    (void)tooltip;
}

/// @brief Mark a tab as modified (shows an unsaved-changes indicator).
void rt_tab_set_modified(void *tab, int64_t modified) {
    (void)tab;
    (void)modified;
}

/// @brief Stub: graphics disabled — returns NULL; no active tab exists.
void *rt_tabbar_get_active(void *tabbar) {
    (void)tabbar;
    return NULL;
}

/// @brief Get the active index of the tabbar.
int64_t rt_tabbar_get_active_index(void *tabbar) {
    (void)tabbar;
    return -1;
}

/// @brief Check if the active tab changed since the last call (edge-triggered).
int64_t rt_tabbar_was_changed(void *tabbar) {
    (void)tabbar;
    return 0;
}

/// @brief Get the number of tabs in the tab bar.
int64_t rt_tabbar_get_tab_count(void *tabbar) {
    (void)tabbar;
    return 0;
}

/// @brief Check if any tab's close button was clicked this frame.
int64_t rt_tabbar_was_close_clicked(void *tabbar) {
    (void)tabbar;
    return 0;
}

/// @brief Get the index of the tab whose close button was clicked (clears after read).
int64_t rt_tabbar_get_close_clicked_index(void *tabbar) {
    (void)tabbar;
    return -1;
}

/// @brief Stub: graphics disabled — returns NULL; no tab exists at any index.
void *rt_tabbar_get_tab_at(void *tabbar, int64_t index) {
    (void)tabbar;
    (void)index;
    return NULL;
}

/// @brief Enable or disable automatic tab removal on close-button click.
void rt_tabbar_set_auto_close(void *tabbar, int64_t auto_close) {
    (void)tabbar;
    (void)auto_close;
}

/// @brief Stub: graphics disabled — returns NULL; no split pane widget is created.
void *rt_splitpane_new(void *parent, int64_t horizontal) {
    (void)parent;
    (void)horizontal;
    return NULL;
}

/// @brief Set the position of the splitpane.
void rt_splitpane_set_position(void *split, double position) {
    (void)split;
    (void)position;
}

/// @brief Get the position of the splitpane.
double rt_splitpane_get_position(void *split) {
    (void)split;
    return 0.5;
}

/// @brief Stub: graphics disabled — returns NULL; no first panel container exists.
void *rt_splitpane_get_first(void *split) {
    (void)split;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no second panel container exists.
void *rt_splitpane_get_second(void *split) {
    (void)split;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no code editor widget is created.
void *rt_codeeditor_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Replace the entire text content of a code editor.
void rt_codeeditor_set_text(void *editor, rt_string text) {
    (void)editor;
    (void)text;
}

/// @brief Retrieve the full text content of a code editor (caller frees the C string).
rt_string rt_codeeditor_get_text(void *editor) {
    (void)editor;
    return rt_str_empty();
}

/// @brief Retrieve the currently selected text in a code editor.
rt_string rt_codeeditor_get_selected_text(void *editor) {
    (void)editor;
    return rt_str_empty();
}

/// @brief Move the cursor to a specific line and column in the code editor.
void rt_codeeditor_set_cursor(void *editor, int64_t line, int64_t col) {
    (void)editor;
    (void)line;
    (void)col;
}

/// @brief Scroll the code editor viewport to make a specific line visible.
void rt_codeeditor_scroll_to_line(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

/// @brief Get the total number of lines in the code editor.
int64_t rt_codeeditor_get_line_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Check whether the code editor's content has been modified since last clear.
int64_t rt_codeeditor_is_modified(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Reset the code editor's modified flag (e.g., after saving).
void rt_codeeditor_clear_modified(void *editor) {
    (void)editor;
}

/// @brief Set the font of the codeeditor.
void rt_codeeditor_set_font(void *editor, void *font, double size) {
    (void)editor;
    (void)font;
    (void)size;
}

/// @brief Get or set the font size of the code editor (in logical points).
double rt_codeeditor_get_font_size(void *editor) {
    (void)editor;
    return 14.0;
}

/// @brief Get or set the font size of the code editor (in logical points).
void rt_codeeditor_set_font_size(void *editor, double size) {
    (void)editor;
    (void)size;
}

/// @brief Switch the active theme to dark mode.
void rt_theme_set_dark(void) {}

/// @brief Switch the active theme to light mode.
void rt_theme_set_light(void) {}

/// @brief Get the name of the theme.
rt_string rt_theme_get_name(void) {
    return rt_string_from_bytes("dark", 4);
}

/// @brief Stub: graphics disabled — returns NULL; no VBox container is created.
void *rt_vbox_new(void) {
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no HBox container is created.
void *rt_hbox_new(void) {
    return NULL;
}

/// @brief Set the spacing of the container.
void rt_container_set_spacing(void *container, double spacing) {
    (void)container;
    (void)spacing;
}

/// @brief Set the padding of the container.
void rt_container_set_padding(void *container, double padding) {
    (void)container;
    (void)padding;
}

/// @brief Check whether the mouse cursor is currently over this widget.
int64_t rt_widget_is_hovered(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Check whether the widget is currently being pressed (mouse down).
int64_t rt_widget_is_pressed(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Check whether the widget currently has keyboard focus.
int64_t rt_widget_is_focused(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Move keyboard focus to a widget.
void rt_widget_focus(void *widget) {
    (void)widget;
}

/// @brief Set the last clicked value.
/// @param widget
void rt_gui_set_last_clicked(void *widget) {
    (void)widget;
}

/// @brief Check whether this widget was clicked during the current frame.
int64_t rt_widget_was_clicked(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Set the position of the widget.
void rt_widget_set_position(void *widget, int64_t x, int64_t y) {
    (void)widget;
    (void)x;
    (void)y;
}

/// @brief Stub: graphics disabled — returns NULL; no dropdown widget is created.
void *rt_dropdown_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Add a selectable item to a dropdown list.
int64_t rt_dropdown_add_item(void *dropdown, rt_string text) {
    (void)dropdown;
    (void)text;
    return -1;
}

/// @brief Remove an item from a dropdown by index.
void rt_dropdown_remove_item(void *dropdown, int64_t index) {
    (void)dropdown;
    (void)index;
}

/// @brief Remove all items from a dropdown, leaving it empty.
void rt_dropdown_clear(void *dropdown) {
    (void)dropdown;
}

/// @brief Programmatically select a dropdown item by index.
void rt_dropdown_set_selected(void *dropdown, int64_t index) {
    (void)dropdown;
    (void)index;
}

/// @brief Get the index of the currently selected dropdown item (-1 if none).
int64_t rt_dropdown_get_selected(void *dropdown) {
    (void)dropdown;
    return -1;
}

/// @brief Get the selected text of the dropdown.
rt_string rt_dropdown_get_selected_text(void *dropdown) {
    (void)dropdown;
    return rt_str_empty();
}

/// @brief Set the placeholder of the dropdown.
void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder) {
    (void)dropdown;
    (void)placeholder;
}

/// @brief Stub: graphics disabled — returns NULL; no slider widget is created.
void *rt_slider_new(void *parent, int64_t horizontal) {
    (void)parent;
    (void)horizontal;
    return NULL;
}

/// @brief Set the value of the slider.
void rt_slider_set_value(void *slider, double value) {
    (void)slider;
    (void)value;
}

/// @brief Get the value of the slider.
double rt_slider_get_value(void *slider) {
    (void)slider;
    return 0.0;
}

/// @brief Set the range of the slider.
void rt_slider_set_range(void *slider, double min_val, double max_val) {
    (void)slider;
    (void)min_val;
    (void)max_val;
}

/// @brief Set the step of the slider.
void rt_slider_set_step(void *slider, double step) {
    (void)slider;
    (void)step;
}

/// @brief Stub: graphics disabled — returns NULL; no progress bar widget is created.
void *rt_progressbar_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Set the value of the progressbar.
void rt_progressbar_set_value(void *progress, double value) {
    (void)progress;
    (void)value;
}

/// @brief Get the value of the progressbar.
double rt_progressbar_get_value(void *progress) {
    (void)progress;
    return 0.0;
}

/// @brief Stub: graphics disabled — returns NULL; no list box widget is created.
void *rt_listbox_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no list item is added.
void *rt_listbox_add_item(void *listbox, rt_string text) {
    (void)listbox;
    (void)text;
    return NULL;
}

/// @brief Remove an item from the listbox.
void rt_listbox_remove_item(void *listbox, void *item) {
    (void)listbox;
    (void)item;
}

/// @brief Remove all entries from the listbox.
void rt_listbox_clear(void *listbox) {
    (void)listbox;
}

/// @brief Programmatically select a listbox item by handle.
void rt_listbox_select(void *listbox, void *item) {
    (void)listbox;
    (void)item;
}

/// @brief Stub: graphics disabled — returns NULL; no selection exists without a list box.
void *rt_listbox_get_selected(void *listbox) {
    (void)listbox;
    return NULL;
}

/// @brief Get the number of items in the listbox.
int64_t rt_listbox_get_count(void *listbox) {
    (void)listbox;
    return 0;
}

/// @brief Get the selected index of the listbox.
int64_t rt_listbox_get_selected_index(void *listbox) {
    (void)listbox;
    return -1;
}

/// @brief Select a listbox item by its zero-based index.
void rt_listbox_select_index(void *listbox, int64_t index) {
    (void)listbox;
    (void)index;
}

/// @brief Check if the listbox selection changed since the last call (edge-triggered).
int64_t rt_listbox_was_selection_changed(void *listbox) {
    (void)listbox;
    return 0;
}

/// @brief Get the display text of a listbox item.
rt_string rt_listbox_item_get_text(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Update the display text of a listbox item.
void rt_listbox_item_set_text(void *item, rt_string text) {
    (void)item;
    (void)text;
}

/// @brief Attach arbitrary string data to a listbox item (replaces previous data).
void rt_listbox_item_set_data(void *item, rt_string data) {
    (void)item;
    (void)data;
}

/// @brief Retrieve the string data previously attached to a listbox item.
rt_string rt_listbox_item_get_data(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Set the font of the listbox.
void rt_listbox_set_font(void *listbox, void *font, double size) {
    (void)listbox;
    (void)font;
    (void)size;
}

/// @brief Stub: graphics disabled — returns NULL; no radio group is created.
void *rt_radiogroup_new(void) {
    return NULL;
}

/// @brief Release resources and destroy the radiogroup.
void rt_radiogroup_destroy(void *group) {
    (void)group;
}

/// @brief Stub: graphics disabled — returns NULL; no radio button widget is created.
void *rt_radiobutton_new(void *parent, rt_string text, void *group) {
    (void)parent;
    (void)text;
    (void)group;
    return NULL;
}

/// @brief Check whether a radio button is currently selected in its group.
int64_t rt_radiobutton_is_selected(void *radio) {
    (void)radio;
    return 0;
}

/// @brief Programmatically select a radio button (deselects siblings in the group).
void rt_radiobutton_set_selected(void *radio, int64_t selected) {
    (void)radio;
    (void)selected;
}

/// @brief Stub: graphics disabled — returns NULL; no spinner widget is created.
void *rt_spinner_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Set the value of the spinner.
void rt_spinner_set_value(void *spinner, double value) {
    (void)spinner;
    (void)value;
}

/// @brief Get the value of the spinner.
double rt_spinner_get_value(void *spinner) {
    (void)spinner;
    return 0.0;
}

/// @brief Set the range of the spinner.
void rt_spinner_set_range(void *spinner, double min_val, double max_val) {
    (void)spinner;
    (void)min_val;
    (void)max_val;
}

/// @brief Set the step of the spinner.
void rt_spinner_set_step(void *spinner, double step) {
    (void)spinner;
    (void)step;
}

/// @brief Set the decimals of the spinner.
void rt_spinner_set_decimals(void *spinner, int64_t decimals) {
    (void)spinner;
    (void)decimals;
}

/// @brief Stub: graphics disabled — returns NULL; no image widget is created.
void *rt_image_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Set the pixels of the image.
void rt_image_set_pixels(void *image, void *pixels, int64_t width, int64_t height) {
    (void)image;
    (void)pixels;
    (void)width;
    (void)height;
}

/// @brief Clear the image widget's pixel data, showing nothing.
void rt_image_clear(void *image) {
    (void)image;
}

/// @brief Set the scale mode of the image.
void rt_image_set_scale_mode(void *image, int64_t mode) {
    (void)image;
    (void)mode;
}

/// @brief Set the opacity of the image.
void rt_image_set_opacity(void *image, double opacity) {
    (void)image;
    (void)opacity;
}

/// @brief Load image file stub (graphics disabled).
int64_t rt_image_load_file(void *image, void *path) {
    (void)image;
    (void)path;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no floating panel is created.
void *rt_floatingpanel_new(void *root) {
    (void)root;
    return NULL;
}

/// @brief Set the position of the floatingpanel.
void rt_floatingpanel_set_position(void *panel, double x, double y) {
    (void)panel;
    (void)x;
    (void)y;
}

/// @brief Set the width and height of a floating panel.
void rt_floatingpanel_set_size(void *panel, double w, double h) {
    (void)panel;
    (void)w;
    (void)h;
}

/// @brief Show or hide a floating panel overlay.
void rt_floatingpanel_set_visible(void *panel, int64_t visible) {
    (void)panel;
    (void)visible;
}

/// @brief Add a child widget to a floating panel's content area.
void rt_floatingpanel_add_child(void *panel, void *child) {
    (void)panel;
    (void)child;
}

#endif /* VIPER_ENABLE_GRAPHICS */
