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

/// @brief Load a font from a file path and return an opaque handle.
/// @details Converts the runtime string path to a C string, loads the font
///          via vg_font_load_file, and returns the raw vg_font_t pointer as
///          an opaque handle. The caller owns the font and must free it with
///          rt_font_destroy when no longer needed. The loaded font is not
///          automatically applied to any widget — use the widget's SetFont
///          method to apply it.
/// @param path File path to a .ttf or .ttc font file (runtime string).
/// @return Opaque font handle, or NULL if the file could not be loaded.
void *rt_font_load(rt_string path) {
    RT_ASSERT_MAIN_THREAD();
    char *cpath = rt_string_to_cstr(path);
    if (!cpath)
        return NULL;

    vg_font_t *font = vg_font_load_file(cpath);
    free(cpath);
    return font;
}

/// @brief Free a previously loaded font and release its resources.
/// @details Destroys the vg_font_t, freeing rasterized glyph caches and the
///          font data buffer. Any widgets still referencing this font will have
///          a dangling pointer — the caller must ensure the font outlives all
///          widgets that use it.
/// @param font Opaque font handle from rt_font_load (safe to pass NULL).
void rt_font_destroy(void *font) {
    RT_ASSERT_MAIN_THREAD();
    if (font) {
        vg_font_destroy((vg_font_t *)font);
    }
}

//=============================================================================
// Widget Functions
//=============================================================================

/// @brief Destroy a widget and its entire subtree, freeing all resources.
/// @details Delegates to vg_widget_destroy, which recursively frees all child
///          widgets. After this call, all pointers to the widget or its
///          descendants are invalid. Safe to call with NULL.
/// @param widget Widget to destroy (opaque handle).
void rt_widget_destroy(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (widget) {
        vg_widget_destroy((vg_widget_t *)widget);
    }
}

/// @brief Show or hide a widget.
/// @details Hidden widgets are skipped during layout, painting, and event
///          dispatch. Child widgets inherit the parent's visibility — hiding
///          a container hides its entire subtree.
/// @param widget  Widget to modify.
/// @param visible Non-zero to show, zero to hide.
void rt_widget_set_visible(void *widget, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    if (widget) {
        vg_widget_set_visible((vg_widget_t *)widget, visible != 0);
    }
}

/// @brief Enable or disable user interaction with a widget.
/// @details Disabled widgets are painted with reduced opacity and do not
///          receive mouse/keyboard events. Unlike visibility, disabled widgets
///          still participate in layout and occupy space.
/// @param widget  Widget to modify.
/// @param enabled Non-zero to enable, zero to disable.
void rt_widget_set_enabled(void *widget, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    if (widget) {
        vg_widget_set_enabled((vg_widget_t *)widget, enabled != 0);
    }
}

/// @brief Set a fixed width and height on the widget.
/// @details Overrides the layout engine's automatic sizing. When a fixed size
///          is set, the widget ignores flex-grow and measures at exactly these
///          dimensions. Pass 0 for either dimension to revert to auto-sizing.
///          Do NOT set a fixed size on the root widget — it is resized
///          dynamically from the window dimensions each frame.
/// @param widget Widget to modify.
/// @param width  Fixed width in logical pixels.
/// @param height Fixed height in logical pixels.
void rt_widget_set_size(void *widget, int64_t width, int64_t height) {
    RT_ASSERT_MAIN_THREAD();
    if (widget) {
        vg_widget_set_fixed_size((vg_widget_t *)widget, (float)width, (float)height);
    }
}

/// @brief Set the flex-grow factor for a widget within a VBox/HBox container.
/// @details The flex value determines how much of the remaining space (after
///          fixed-size and auto-sized widgets) this widget claims. A flex of 1.0
///          means equal share; 2.0 means double share relative to flex-1 siblings.
///          A flex of 0.0 means the widget only takes its natural/fixed size.
/// @param widget Widget to modify.
/// @param flex   Flex-grow factor (>= 0.0).
void rt_widget_set_flex(void *widget, double flex) {
    RT_ASSERT_MAIN_THREAD();
    if (widget) {
        vg_widget_set_flex((vg_widget_t *)widget, (float)flex);
    }
}

/// @brief Add a child widget to a parent container.
/// @details The parent handle can be either an app handle (uses app->root) or
///          a widget pointer. The child is appended to the parent's child list
///          and will participate in layout and painting during the next frame.
///          The child's lifetime is now tied to the parent — destroying the
///          parent destroys all children recursively.
/// @param parent Parent container or app handle.
/// @param child  Widget to add as a child.
void rt_widget_add_child(void *parent, void *child) {
    RT_ASSERT_MAIN_THREAD();
    if (parent && child) {
        vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
        if (parent_widget)
            vg_widget_add_child(parent_widget, (vg_widget_t *)child);
    }
}

/// @brief Set uniform margin (external spacing) around a widget.
/// @details Margin is the space between this widget's outer edge and its
///          siblings or parent boundary. Applied equally on all four sides.
/// @param widget Widget to modify.
/// @param margin Margin in logical pixels.
void rt_widget_set_margin(void *widget, int64_t margin) {
    RT_ASSERT_MAIN_THREAD();
    if (widget)
        vg_widget_set_margin((vg_widget_t *)widget, (float)margin);
}

/// @brief Set the tab-order index for keyboard navigation.
/// @details Widgets with explicit tab indices (>= 0) are visited in ascending
///          order during Tab/Shift+Tab navigation. Widgets with index -1
///          (default) are visited in document order (depth-first traversal).
/// @param widget Widget to modify.
/// @param idx    Tab index (>= 0 for explicit ordering, -1 for default DFS).
void rt_widget_set_tab_index(void *widget, int64_t idx) {
    RT_ASSERT_MAIN_THREAD();
    if (widget)
        vg_widget_set_tab_index((vg_widget_t *)widget, (int)idx);
}

/// @brief Check whether the widget is currently visible.
/// @param widget Widget to query.
/// @return 1 if visible, 0 if hidden or NULL.
int64_t rt_widget_is_visible(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return ((vg_widget_t *)widget)->visible ? 1 : 0;
}

/// @brief Check whether the widget is currently enabled for interaction.
/// @param widget Widget to query.
/// @return 1 if enabled, 0 if disabled or NULL.
int64_t rt_widget_is_enabled(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return ((vg_widget_t *)widget)->enabled ? 1 : 0;
}

/// @brief Get the current laid-out width of the widget in physical pixels.
/// @param widget Widget to query.
/// @return Width in pixels, or 0 if NULL.
int64_t rt_widget_get_width(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->width;
}

/// @brief Get the current laid-out height of the widget in physical pixels.
/// @param widget Widget to query.
/// @return Height in pixels, or 0 if NULL.
int64_t rt_widget_get_height(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->height;
}

/// @brief Get the widget's X position relative to its parent.
/// @param widget Widget to query.
/// @return X offset in pixels, or 0 if NULL.
int64_t rt_widget_get_x(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->x;
}

/// @brief Get the widget's Y position relative to its parent.
/// @param widget Widget to query.
/// @return Y offset in pixels, or 0 if NULL.
int64_t rt_widget_get_y(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return (int64_t)((vg_widget_t *)widget)->y;
}

/// @brief Get the widget's flex-grow factor.
/// @param widget Widget to query.
/// @return Flex value, or 0.0 if NULL.
double rt_widget_get_flex(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0.0;
    return (double)((vg_widget_t *)widget)->layout.flex;
}

//=============================================================================
// Label Widget
//=============================================================================

/// @brief Create a new text label widget.
/// @details Creates a vg_label_t as a child of the given parent, sets its
///          initial text, and applies the app's default font. Labels are
///          read-only display widgets — they show static text and do not
///          accept user input or focus.
/// @param parent Parent container or app handle.
/// @param text   Initial display text (runtime string).
/// @return Opaque label widget handle, or NULL on failure.
void *rt_label_new(void *parent, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    char *ctext = rt_string_to_cstr(text);
    vg_label_t *label = vg_label_create(parent_widget, ctext ? ctext : "");
    free(ctext);
    rt_gui_apply_default_font((vg_widget_t *)label);
    return label;
}

/// @brief Update the display text of a label widget.
/// @details The vg layer copies the text internally, so the temporary C string
///          is freed immediately after the call.
/// @param label Label widget handle.
/// @param text  New text content (runtime string).
void rt_label_set_text(void *label, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!label)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_label_set_text((vg_label_t *)label, ctext);
    free(ctext);
}

/// @brief Override the font and size used by a label widget.
/// @details Replaces the label's font with a user-provided font. The font
///          pointer is borrowed — the label does not take ownership, so the
///          caller must ensure the font outlives the label.
/// @param label Label widget handle.
/// @param font  Font handle from rt_font_load.
/// @param size  Font size in points.
void rt_label_set_font(void *label, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (label) {
        vg_label_set_font((vg_label_t *)label, (vg_font_t *)font, (float)size);
    }
}

/// @brief Set the text color of a label as a packed ARGB integer.
void rt_label_set_color(void *label, int64_t color) {
    RT_ASSERT_MAIN_THREAD();
    if (label) {
        vg_label_set_color((vg_label_t *)label, (uint32_t)color);
    }
}

//=============================================================================
// Button Widget
//=============================================================================

/// @brief Create a new push button widget.
/// @details Creates a vg_button_t with the given label text, adds it as a child
///          of the parent container, and applies the app's default font. Buttons
///          support click detection (via rt_widget_was_clicked), optional icons,
///          and visual styles (primary, secondary, danger, etc.).
/// @param parent Parent container or app handle.
/// @param text   Button label text (runtime string).
/// @return Opaque button widget handle, or NULL on failure.
void *rt_button_new(void *parent, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    char *ctext = rt_string_to_cstr(text);
    vg_button_t *button = vg_button_create(parent_widget, ctext);
    free(ctext);
    rt_gui_apply_default_font((vg_widget_t *)button);
    return button;
}

/// @brief Update the label text displayed on a button.
void rt_button_set_text(void *button, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!button)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_button_set_text((vg_button_t *)button, ctext);
    free(ctext);
}

/// @brief Override the font and size used by a button widget.
/// @param button Button widget handle.
/// @param font   Font handle from rt_font_load (borrowed, not owned).
/// @param size   Font size in points.
void rt_button_set_font(void *button, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (button) {
        vg_button_set_font((vg_button_t *)button, (vg_font_t *)font, (float)size);
    }
}

/// @brief Set the visual style preset for a button (primary, secondary, danger, etc.).
/// @details Button styles control the background/border/text color scheme. The
///          style enum maps to vg_button_style_t values.
/// @param button Button widget handle.
/// @param style  Style enum value (0 = default, 1 = primary, 2 = danger, etc.).
void rt_button_set_style(void *button, int64_t style) {
    RT_ASSERT_MAIN_THREAD();
    if (button) {
        vg_button_set_style((vg_button_t *)button, (vg_button_style_t)style);
    }
}

/// @brief Set a text/glyph icon to display alongside the button label.
/// @details The icon string is typically a single Unicode glyph (e.g., from an
///          icon font). The vg layer copies the string, so the temporary C
///          string is freed immediately. Use rt_button_set_icon_pos to control
///          whether the icon appears left or right of the label.
/// @param button Button widget handle.
/// @param icon   Icon glyph or text (runtime string).
void rt_button_set_icon(void *button, rt_string icon) {
    RT_ASSERT_MAIN_THREAD();
    if (!button)
        return;
    char *cicon = rt_string_to_cstr(icon);
    vg_button_set_icon((vg_button_t *)button, cicon);
    free(cicon);
}

/// @brief Set the icon position relative to the button label.
/// @details 0 = icon on the left (default), 1 = icon on the right. The icon
///          is drawn with a 4 px gap from the label text.
void rt_button_set_icon_pos(void *button, int64_t pos) {
    RT_ASSERT_MAIN_THREAD();
    if (button)
        vg_button_set_icon_position((vg_button_t *)button, (int)pos);
}

//=============================================================================
// TextInput Widget
//=============================================================================

/// @brief Create a new single-line text input widget.
/// @details Creates a vg_textinput_t with an integrated undo/redo stack. The
///          initial empty string is pushed onto the undo stack at creation.
///          Each subsequent edit (insert/delete) pushes the new state. The
///          widget supports keyboard focus, cursor movement, text selection,
///          clipboard operations (Ctrl+C/V/X), and Tab-based focus traversal.
/// @param parent Parent container or app handle.
/// @return Opaque text input widget handle, or NULL on failure.
void *rt_textinput_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    vg_textinput_t *input = vg_textinput_create(parent_widget);
    rt_gui_apply_default_font((vg_widget_t *)input);
    return input;
}

/// @brief Programmatically set the text content of a text input.
/// @details Replaces the entire content. Does not push to the undo stack
///          (programmatic changes are not undoable by the user).
/// @param input Text input widget handle.
/// @param text  New text content (runtime string).
void rt_textinput_set_text(void *input, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_textinput_set_text((vg_textinput_t *)input, ctext);
    free(ctext);
}

/// @brief Retrieve the current text content of a text input.
/// @details Returns a runtime string copy of the widget's internal buffer.
///          The returned string is GC-managed and safe to use from Zia code.
/// @param input Text input widget handle.
/// @return Current text as a runtime string, or empty string if NULL.
rt_string rt_textinput_get_text(void *input) {
    RT_ASSERT_MAIN_THREAD();
    if (!input)
        return rt_str_empty();
    const char *text = vg_textinput_get_text((vg_textinput_t *)input);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

/// @brief Set the placeholder text shown when the input is empty.
/// @details The placeholder appears in a dimmed style and disappears when the
///          user starts typing. Useful for hinting at expected input format.
void rt_textinput_set_placeholder(void *input, rt_string placeholder) {
    RT_ASSERT_MAIN_THREAD();
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_textinput_set_placeholder((vg_textinput_t *)input, ctext);
    free(ctext);
}

/// @brief Set the font of the textinput.
void rt_textinput_set_font(void *input, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (input) {
        vg_textinput_set_font((vg_textinput_t *)input, (vg_font_t *)font, (float)size);
    }
}

//=============================================================================
// Checkbox Widget
//=============================================================================

/// @brief Create a new checkbox widget with a label.
/// @details Creates a vg_checkbox_t with the given label text. Checkboxes
///          toggle between checked and unchecked states when clicked. Use
///          rt_checkbox_is_checked to poll the current state each frame.
/// @param parent Parent container or app handle.
/// @param text   Label text displayed next to the checkbox (runtime string).
/// @return Opaque checkbox widget handle, or NULL on failure.
void *rt_checkbox_new(void *parent, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    char *ctext = rt_string_to_cstr(text);
    vg_checkbox_t *checkbox = vg_checkbox_create(parent_widget, ctext);
    free(ctext);
    rt_gui_apply_default_font((vg_widget_t *)checkbox);
    return checkbox;
}

/// @brief Programmatically set the checked state of a checkbox.
/// @param checkbox Checkbox widget handle.
/// @param checked  Non-zero to check, zero to uncheck.
void rt_checkbox_set_checked(void *checkbox, int64_t checked) {
    RT_ASSERT_MAIN_THREAD();
    if (checkbox) {
        vg_checkbox_set_checked((vg_checkbox_t *)checkbox, checked != 0);
    }
}

/// @brief Query whether a checkbox is currently checked.
/// @param checkbox Checkbox widget handle.
/// @return 1 if checked, 0 if unchecked or NULL.
int64_t rt_checkbox_is_checked(void *checkbox) {
    RT_ASSERT_MAIN_THREAD();
    if (!checkbox)
        return 0;
    return vg_checkbox_is_checked((vg_checkbox_t *)checkbox) ? 1 : 0;
}

/// @brief Update the label text displayed next to a checkbox.
/// @param checkbox Checkbox widget handle.
/// @param text     New label text (runtime string).
void rt_checkbox_set_text(void *checkbox, rt_string text) {
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

/// @brief Create a new scrollable container widget.
/// @details Creates a vg_scrollview_t that clips its content to its viewport
///          bounds and provides scrollbars when the content exceeds the
///          viewport. Children are added to the scroll view's internal
///          content container, not directly to the scroll view itself.
/// @param parent Parent container or app handle.
/// @return Opaque scroll view widget handle, or NULL on failure.
void *rt_scrollview_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    return vg_scrollview_create(rt_gui_widget_parent_from_handle(parent));
}

/// @brief Programmatically set the scroll position of a scroll view.
/// @details Scrolls the content to the specified (x, y) offset. Values are
///          clamped to [0, content_size - viewport_size] by the vg layout engine.
/// @param scroll Scroll view widget handle.
/// @param x      Horizontal scroll offset in pixels.
/// @param y      Vertical scroll offset in pixels.
void rt_scrollview_set_scroll(void *scroll, double x, double y) {
    RT_ASSERT_MAIN_THREAD();
    if (scroll) {
        vg_scrollview_set_scroll((vg_scrollview_t *)scroll, (float)x, (float)y);
    }
}

/// @brief Set the total content size of a scroll view (determines scroll range).
/// @details The content size defines the virtual area that can be scrolled. If
///          the content is larger than the viewport, scrollbars appear. Set
///          this to match the actual size of the content you're displaying.
/// @param scroll Scroll view widget handle.
/// @param width  Total content width in pixels.
/// @param height Total content height in pixels.
void rt_scrollview_set_content_size(void *scroll, double width, double height) {
    RT_ASSERT_MAIN_THREAD();
    if (scroll) {
        vg_scrollview_set_content_size((vg_scrollview_t *)scroll, (float)width, (float)height);
    }
}

/// @brief Get the current horizontal scroll offset.
double rt_scrollview_get_scroll_x(void *scroll) {
    RT_ASSERT_MAIN_THREAD();
    if (!scroll)
        return 0.0;
    float x = 0.0f, y = 0.0f;
    vg_scrollview_get_scroll((vg_scrollview_t *)scroll, &x, &y);
    return (double)x;
}

/// @brief Get the current vertical scroll offset.
double rt_scrollview_get_scroll_y(void *scroll) {
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

/// @brief Create a new hierarchical tree view widget.
/// @details Creates a vg_treeview_t for displaying expandable/collapsible
///          tree structures (e.g., file browsers, scene graphs). Nodes can
///          be expanded, collapsed, selected, and carry user-data strings.
///          Selection changes are edge-triggered via rt_treeview_was_selection_changed.
/// @param parent Parent container or app handle.
/// @return Opaque tree view widget handle, or NULL on failure.
void *rt_treeview_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    vg_treeview_t *tv = vg_treeview_create(parent_widget);
    if (tv) {
        if (app)
            rt_gui_activate_app(app);
        rt_gui_ensure_default_font();
        if (app && app->default_font)
            vg_treeview_set_font(tv, app->default_font, app->default_font_size);
    }
    return tv;
}

/// @brief Add a child node to the tree view (or to a parent node).
/// @details If parent_node is NULL, the node is added at the root level.
///          The text is copied by the vg layer. Returns the new node handle,
///          which can be used to add further children or set user data.
/// @param tree        Tree view widget handle.
/// @param parent_node Parent node handle, or NULL for root-level.
/// @param text        Node label text (runtime string).
/// @return New node handle, or NULL on failure.
void *rt_treeview_add_node(void *tree, void *parent_node, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!tree)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_tree_node_t *node =
        vg_treeview_add_node((vg_treeview_t *)tree, (vg_tree_node_t *)parent_node, ctext);
    free(ctext);
    return node;
}

/// @brief Remove a node and its subtree from the tree view.
void rt_treeview_remove_node(void *tree, void *node) {
    RT_ASSERT_MAIN_THREAD();
    if (tree && node) {
        vg_treeview_remove_node((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Remove all nodes from the tree view, leaving it empty.
void rt_treeview_clear(void *tree) {
    RT_ASSERT_MAIN_THREAD();
    if (tree) {
        vg_treeview_clear((vg_treeview_t *)tree);
    }
}

/// @brief Expand a tree node to show its children.
void rt_treeview_expand(void *tree, void *node) {
    RT_ASSERT_MAIN_THREAD();
    if (tree && node) {
        vg_treeview_expand((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Collapse a tree node to hide its children.
void rt_treeview_collapse(void *tree, void *node) {
    RT_ASSERT_MAIN_THREAD();
    if (tree && node) {
        vg_treeview_collapse((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Programmatically select a tree node (NULL to clear selection).
void rt_treeview_select(void *tree, void *node) {
    RT_ASSERT_MAIN_THREAD();
    if (tree) {
        vg_treeview_select((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

/// @brief Set the font of the treeview.
void rt_treeview_set_font(void *tree, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (tree) {
        vg_treeview_set_font((vg_treeview_t *)tree, (vg_font_t *)font, (float)size);
    }
}

void *rt_treeview_get_selected(void *tree) {
    RT_ASSERT_MAIN_THREAD();
    if (!tree)
        return NULL;
    vg_treeview_t *tv = (vg_treeview_t *)tree;
    return tv->selected;
}

/// @brief Check if the tree view selection changed since the last call (edge-triggered).
int64_t rt_treeview_was_selection_changed(void *tree) {
    RT_ASSERT_MAIN_THREAD();
    if (!tree)
        return 0;
    vg_treeview_t *tv = (vg_treeview_t *)tree;

    // Per-instance selection tracking using prev_selected field
    // (matches the pattern used by rt_tabbar_was_changed / prev_active_tab).
    if (tv->selected != tv->prev_selected) {
        tv->prev_selected = tv->selected;
        return 1;
    }
    return 0;
}

/// @brief Get the display text of a tree node.
rt_string rt_treeview_node_get_text(void *node) {
    RT_ASSERT_MAIN_THREAD();
    if (!node)
        return rt_str_empty();
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    if (!n->text)
        return rt_str_empty();
    return rt_string_from_bytes(n->text, strlen(n->text));
}

/// @brief Attach arbitrary string data to a tree node (replaces any previous data).
void rt_treeview_node_set_data(void *node, rt_string data) {
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

/// @brief Retrieve the string data previously attached to a tree node.
rt_string rt_treeview_node_get_data(void *node) {
    RT_ASSERT_MAIN_THREAD();
    if (!node)
        return rt_str_empty();
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    if (!n->user_data)
        return rt_str_empty();
    const char *data = (const char *)n->user_data;
    return rt_string_from_bytes(data, strlen(data));
}

/// @brief Check whether a tree node is currently in the expanded state.
int64_t rt_treeview_node_is_expanded(void *node) {
    RT_ASSERT_MAIN_THREAD();
    if (!node)
        return 0;
    vg_tree_node_t *n = (vg_tree_node_t *)node;
    return n->expanded ? 1 : 0;
}

#else /* !VIPER_ENABLE_GRAPHICS */

void *rt_font_load(rt_string path) {
    (void)path;
    return NULL;
}

/// @brief Release resources and destroy the font.
void rt_font_destroy(void *font) {
    (void)font;
}

/// @brief Release resources and destroy the widget.
void rt_widget_destroy(void *widget) {
    (void)widget;
}

/// @brief Show or hide a widget.
void rt_widget_set_visible(void *widget, int64_t visible) {
    (void)widget;
    (void)visible;
}

/// @brief Enable or disable user interaction with a widget.
void rt_widget_set_enabled(void *widget, int64_t enabled) {
    (void)widget;
    (void)enabled;
}

/// @brief Set a fixed width and height on the widget.
void rt_widget_set_size(void *widget, int64_t width, int64_t height) {
    (void)widget;
    (void)width;
    (void)height;
}

/// @brief Set the flex-grow factor for a widget.
void rt_widget_set_flex(void *widget, double flex) {
    (void)widget;
    (void)flex;
}

/// @brief Add a child widget to a parent container.
void rt_widget_add_child(void *parent, void *child) {
    (void)parent;
    (void)child;
}

/// @brief Set the margin of the widget.
void rt_widget_set_margin(void *widget, int64_t margin) {
    (void)widget;
    (void)margin;
}

/// @brief Set the tab index of the widget.
void rt_widget_set_tab_index(void *widget, int64_t idx) {
    (void)widget;
    (void)idx;
}

/// @brief Check whether the widget is currently visible.
int64_t rt_widget_is_visible(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Check whether the widget is currently enabled.
int64_t rt_widget_is_enabled(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Get the width of the widget.
int64_t rt_widget_get_width(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Get the height of the widget.
int64_t rt_widget_get_height(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Get the x of the widget.
int64_t rt_widget_get_x(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Get the y of the widget.
int64_t rt_widget_get_y(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Get the flex of the widget.
double rt_widget_get_flex(void *widget) {
    (void)widget;
    return 0.0;
}

void *rt_label_new(void *parent, rt_string text) {
    (void)parent;
    (void)text;
    return NULL;
}

/// @brief Set the text of the label.
void rt_label_set_text(void *label, rt_string text) {
    (void)label;
    (void)text;
}

/// @brief Set the font of the label.
void rt_label_set_font(void *label, void *font, double size) {
    (void)label;
    (void)font;
    (void)size;
}

/// @brief Set the color of the label.
void rt_label_set_color(void *label, int64_t color) {
    (void)label;
    (void)color;
}

void *rt_button_new(void *parent, rt_string text) {
    (void)parent;
    (void)text;
    return NULL;
}

/// @brief Set the text of the button.
void rt_button_set_text(void *button, rt_string text) {
    (void)button;
    (void)text;
}

/// @brief Set the font of the button.
void rt_button_set_font(void *button, void *font, double size) {
    (void)button;
    (void)font;
    (void)size;
}

/// @brief Set the style of the button.
void rt_button_set_style(void *button, int64_t style) {
    (void)button;
    (void)style;
}

/// @brief Set the icon of the button.
void rt_button_set_icon(void *button, rt_string icon) {
    (void)button;
    (void)icon;
}

/// @brief Set the icon pos of the button.
void rt_button_set_icon_pos(void *button, int64_t pos) {
    (void)button;
    (void)pos;
}

void *rt_textinput_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Set the text of the textinput.
void rt_textinput_set_text(void *input, rt_string text) {
    (void)input;
    (void)text;
}

/// @brief Get the text of the textinput.
rt_string rt_textinput_get_text(void *input) {
    (void)input;
    return rt_str_empty();
}

/// @brief Set the placeholder of the textinput.
void rt_textinput_set_placeholder(void *input, rt_string placeholder) {
    (void)input;
    (void)placeholder;
}

/// @brief Set the font of the textinput.
void rt_textinput_set_font(void *input, void *font, double size) {
    (void)input;
    (void)font;
    (void)size;
}

void *rt_checkbox_new(void *parent, rt_string text) {
    (void)parent;
    (void)text;
    return NULL;
}

/// @brief Programmatically set the checked state of a checkbox.
/// @param checkbox
/// @param checked
void rt_checkbox_set_checked(void *checkbox, int64_t checked) {
    (void)checkbox;
    (void)checked;
}

/// @brief Query whether a checkbox is currently checked.
/// @param checkbox
/// @return Result value.
int64_t rt_checkbox_is_checked(void *checkbox) {
    (void)checkbox;
    return 0;
}

/// @brief Update the label text displayed next to a checkbox.
/// @param checkbox
/// @param text
void rt_checkbox_set_text(void *checkbox, rt_string text) {
    (void)checkbox;
    (void)text;
}

void *rt_scrollview_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Set the scroll of the scrollview.
void rt_scrollview_set_scroll(void *scroll, double x, double y) {
    (void)scroll;
    (void)x;
    (void)y;
}

/// @brief Set the content size of a scroll view.
void rt_scrollview_set_content_size(void *scroll, double width, double height) {
    (void)scroll;
    (void)width;
    (void)height;
}

/// @brief Get the scroll x of the scrollview.
double rt_scrollview_get_scroll_x(void *scroll) {
    (void)scroll;
    return 0.0;
}

/// @brief Get the current vertical scroll offset.
double rt_scrollview_get_scroll_y(void *scroll) {
    (void)scroll;
    return 0.0;
}

void *rt_treeview_new(void *parent) {
    (void)parent;
    return NULL;
}

void *rt_treeview_add_node(void *tree, void *parent_node, rt_string text) {
    (void)tree;
    (void)parent_node;
    (void)text;
    return NULL;
}

/// @brief Remove a node and its subtree from the tree view.
void rt_treeview_remove_node(void *tree, void *node) {
    (void)tree;
    (void)node;
}

/// @brief Remove all nodes from the tree view, leaving it empty.
void rt_treeview_clear(void *tree) {
    (void)tree;
}

/// @brief Expand a tree node to show its children.
void rt_treeview_expand(void *tree, void *node) {
    (void)tree;
    (void)node;
}

/// @brief Collapse a tree node to hide its children.
void rt_treeview_collapse(void *tree, void *node) {
    (void)tree;
    (void)node;
}

/// @brief Programmatically select a tree node (NULL to clear selection).
void rt_treeview_select(void *tree, void *node) {
    (void)tree;
    (void)node;
}

/// @brief Set the font of the treeview.
void rt_treeview_set_font(void *tree, void *font, double size) {
    (void)tree;
    (void)font;
    (void)size;
}

void *rt_treeview_get_selected(void *tree) {
    (void)tree;
    return NULL;
}

/// @brief Check if the tree view selection changed since the last call (edge-triggered).
int64_t rt_treeview_was_selection_changed(void *tree) {
    (void)tree;
    return 0;
}

/// @brief Get the display text of a tree node.
rt_string rt_treeview_node_get_text(void *node) {
    (void)node;
    return rt_str_empty();
}

/// @brief Attach arbitrary string data to a tree node (replaces any previous data).
void rt_treeview_node_set_data(void *node, rt_string data) {
    (void)node;
    (void)data;
}

/// @brief Retrieve the string data previously attached to a tree node.
rt_string rt_treeview_node_get_data(void *node) {
    (void)node;
    return rt_str_empty();
}

/// @brief Check whether a tree node is currently in the expanded state.
int64_t rt_treeview_node_is_expanded(void *node) {
    (void)node;
    return 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
