//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/include/vg_ide_widgets_panels.h
// Purpose: SplitPane, TabBar, OutputPane, and Breadcrumb widget declarations —
//          layout primitives for multi-pane IDE-style shells.
// Key invariants:
//   - All create functions return NULL on allocation failure.
//   - String parameters are copied internally unless documented otherwise.
//   - SplitPane divider positions are stored as fractions in [0.0, 1.0].
// Ownership/Lifetime:
//   - Widgets are owned by their parent in the widget tree.
// Links: lib/gui/include/vg_ide_widgets_common.h,
//        lib/gui/include/vg_widget.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vg_ide_widgets_common.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// TabBar Widget
//=============================================================================

/// @brief Tab structure
typedef struct vg_tab {
    uint64_t magic;          ///< Live-tab sentinel for stale handle detection
    struct vg_tabbar *owner; ///< Owning tab bar for invalidation/reorder
    char *title;             ///< Tab title (owned)
    char *tooltip;           ///< Tab tooltip (owned)
    void *user_data;         ///< User data
    bool closable;           ///< Can tab be closed
    bool modified;           ///< Show modified indicator
    struct vg_tab *next;
    struct vg_tab *prev;
    struct vg_tab *retired_next; ///< Retired-tab list link
} vg_tab_t;

/// @brief Tab callbacks
typedef void (*vg_tab_select_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
typedef bool (*vg_tab_close_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
typedef void (*vg_tab_reorder_callback_t)(vg_widget_t *tabbar,
                                          vg_tab_t *tab,
                                          int new_index,
                                          void *user_data);

/// @brief TabBar widget structure
typedef struct vg_tabbar {
    vg_widget_t base;

    vg_tab_t *first_tab;    ///< First tab
    vg_tab_t *last_tab;     ///< Last tab
    vg_tab_t *active_tab;   ///< Currently active tab
    vg_tab_t *retired_tabs; ///< Removed tabs kept until tabbar destroy for stale handle checks
    int tab_count;          ///< Number of tabs

    vg_font_t *font; ///< Font for rendering
    float font_size; ///< Font size

    // Appearance
    float tab_height;        ///< Tab height
    float tab_padding;       ///< Tab horizontal padding
    float close_button_size; ///< Close button size
    float max_tab_width;     ///< Maximum tab width
    uint32_t active_bg;      ///< Active tab background
    uint32_t inactive_bg;    ///< Inactive tab background
    uint32_t text_color;     ///< Text color
    uint32_t close_color;    ///< Close button color

    // Scrolling (for many tabs)
    float scroll_x;    ///< Horizontal scroll offset
    float total_width; ///< Total width of all tabs

    // Callbacks
    vg_tab_select_callback_t on_select;
    void *on_select_data;
    vg_tab_close_callback_t on_close;
    void *on_close_data;
    vg_tab_reorder_callback_t on_reorder;
    void *on_reorder_data;

    // State
    vg_tab_t *hovered_tab;       ///< Currently hovered tab
    bool close_button_hovered;   ///< Is close button hovered
    bool dragging;               ///< Is dragging a tab
    bool drag_pending;           ///< Pointer is captured and may become a drag after threshold
    vg_tab_t *drag_tab;          ///< Tab being dragged
    float drag_origin_x;         ///< Mouse-down X position for drag-threshold checks
    float drag_x;                ///< Drag position
    vg_tab_t *pressed_tab;       ///< Tab pressed on mouse-down, committed on mouse-up
    vg_tab_t *pressed_close_tab; ///< Close button pressed on mouse-down, committed on mouse-up

    // Per-frame tracking for Zia runtime
    vg_tab_t *prev_active_tab;    ///< Previous active tab (for change detection)
    int close_clicked_index;      ///< Last close-click index (cleared on index read, -1 = none)
    uint64_t close_click_version; ///< Monotonic counter for close-click events
    uint64_t reported_close_click_version;   ///< Last close-click version observed by runtime
    bool auto_close;                         ///< Auto-remove tab on close click (default true)
    uint64_t active_change_version;          ///< Monotonic counter for active-tab changes
    uint64_t reported_active_change_version; ///< Last active-change version observed by runtime
    char *saved_tooltip_text;  ///< Preserved widget tooltip while a tab tooltip is active
    bool hover_tooltip_active; ///< True while widget tooltip is overridden by hovered tab
} vg_tabbar_t;

/// @brief Create a new tab bar widget.
/// @param parent Parent widget (can be NULL).
/// @return New tab bar or NULL on failure.
vg_tabbar_t *vg_tabbar_create(vg_widget_t *parent);

/// @brief Add a new tab to the end of the tab bar.
/// @param tabbar   Tab bar widget.
/// @param title    Tab label text (copied internally).
/// @param closable true if the tab should show a close button.
/// @return Newly added tab, or NULL on failure.
vg_tab_t *vg_tabbar_add_tab(vg_tabbar_t *tabbar, const char *title, bool closable);

/// @brief Return true if a tab handle is still live and owned by a tab bar.
/// @param tab Tab handle to test.
/// @return true when the handle points at a currently live tab.
bool vg_tab_is_live(const vg_tab_t *tab);

/// @brief Remove a tab, retiring the stale handle until destroy or prune.
/// @param tabbar Tab bar widget.
/// @param tab    Tab to remove (must belong to @p tabbar).
void vg_tabbar_remove_tab(vg_tabbar_t *tabbar, vg_tab_t *tab);

/// @brief Free retired tab tombstones after all stale tab handles are discarded.
/// @param tabbar Tab bar widget.
void vg_tabbar_prune_retired_tabs(vg_tabbar_t *tabbar);

/// @brief Make a tab the active (foreground) tab.
/// @param tabbar Tab bar widget.
/// @param tab    Tab to activate (must belong to @p tabbar).
void vg_tabbar_set_active(vg_tabbar_t *tabbar, vg_tab_t *tab);

/// @brief Get the currently active tab.
/// @param tabbar Tab bar widget.
/// @return Active tab pointer, or NULL if the bar is empty.
vg_tab_t *vg_tabbar_get_active(vg_tabbar_t *tabbar);

/// @brief Get the zero-based index of a tab within the bar.
/// @param tabbar Tab bar widget.
/// @param tab    Tab to locate.
/// @return Zero-based index, or -1 if the tab does not belong to @p tabbar.
int vg_tabbar_get_tab_index(vg_tabbar_t *tabbar, vg_tab_t *tab);

/// @brief Get the tab at a zero-based index.
/// @param tabbar Tab bar widget.
/// @param index  Zero-based index.
/// @return Tab at @p index, or NULL if the index is out of range.
vg_tab_t *vg_tabbar_get_tab_at(vg_tabbar_t *tabbar, int index);

/// @brief Get the zero-based index of the tab under canvas coordinates (x, y).
/// @param tabbar Tab bar widget.
/// @param x      Canvas-pixel X (input-layer coordinate space).
/// @param y      Canvas-pixel Y.
/// @return Tab index at the point, or -1 if none.
int vg_tabbar_index_at(vg_tabbar_t *tabbar, int x, int y);

/// @brief Set the label text on an existing tab.
/// @param tab   Tab to modify.
/// @param title New title string (copied internally).
void vg_tab_set_title(vg_tab_t *tab, const char *title);

/// @brief Set the modified indicator on a tab.
/// @param tab      Tab to modify.
/// @param modified true to show the unsaved-changes dot.
void vg_tab_set_modified(vg_tab_t *tab, bool modified);

/// @brief Set the tooltip text shown when hovering over a tab.
/// @param tab     Tab to modify.
/// @param tooltip Tooltip string (copied internally); NULL clears it.
void vg_tab_set_tooltip(vg_tab_t *tab, const char *tooltip);

/// @brief Set arbitrary caller data associated with a tab.
/// @param tab  Tab to modify.
/// @param data Caller-owned pointer stored without copying.
void vg_tab_set_data(vg_tab_t *tab, void *data);

/// @brief Set the font used to render tab labels.
/// @param tabbar Tab bar widget.
/// @param font   Font handle.
/// @param size   Font size in pixels.
void vg_tabbar_set_font(vg_tabbar_t *tabbar, vg_font_t *font, float size);

/// @brief Set the callback fired when the active tab changes.
/// @param tabbar    Tab bar widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_tabbar_set_on_select(vg_tabbar_t *tabbar,
                             vg_tab_select_callback_t callback,
                             void *user_data);

/// @brief Set the callback fired when a close button is clicked.
/// @param tabbar    Tab bar widget.
/// @param callback  Handler; return true to allow removal, false to cancel.
/// @param user_data User data passed to the handler.
void vg_tabbar_set_on_close(vg_tabbar_t *tabbar, vg_tab_close_callback_t callback, void *user_data);

/// @brief Set the callback fired when a tab is dragged to a new position.
/// @param tabbar    Tab bar widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_tabbar_set_on_reorder(vg_tabbar_t *tabbar,
                              vg_tab_reorder_callback_t callback,
                              void *user_data);

//=============================================================================
// SplitPane Widget
//=============================================================================

/// @brief Split direction
typedef enum vg_split_direction {
    VG_SPLIT_HORIZONTAL, ///< Left/Right split
    VG_SPLIT_VERTICAL    ///< Top/Bottom split
} vg_split_direction_t;

/// @brief SplitPane widget structure
typedef struct vg_splitpane {
    vg_widget_t base;

    vg_split_direction_t direction; ///< Split direction
    float split_position;           ///< Splitter position (0-1 ratio)
    float min_first_size;           ///< Minimum size for first pane
    float min_second_size;          ///< Minimum size for second pane
    float splitter_size;            ///< Splitter bar thickness

    uint32_t splitter_color;       ///< Splitter bar color
    uint32_t splitter_hover_color; ///< Splitter hover color

    // State
    bool splitter_hovered;  ///< Is splitter hovered
    bool dragging;          ///< Is dragging splitter
    float drag_start;       ///< Drag start position
    float drag_start_split; ///< Split position at drag start
} vg_splitpane_t;

/// @brief Create a new split pane widget.
/// @param parent    Parent widget (can be NULL).
/// @param direction VG_SPLIT_HORIZONTAL (left/right) or VG_SPLIT_VERTICAL (top/bottom).
/// @return New split pane or NULL on failure.
vg_splitpane_t *vg_splitpane_create(vg_widget_t *parent, vg_split_direction_t direction);

/// @brief Set the divider position as a normalised fraction.
/// @param split    Split pane widget.
/// @param position Fraction in [0.0, 1.0] where 0.5 places the divider at the midpoint.
void vg_splitpane_set_position(vg_splitpane_t *split, float position);

/// @brief Get the current divider position.
/// @param split Split pane widget.
/// @return Current position fraction in [0.0, 1.0].
float vg_splitpane_get_position(vg_splitpane_t *split);

/// @brief Set the minimum pixel sizes for each pane.
/// @param split      Split pane widget.
/// @param min_first  Minimum size of the first (left/top) pane in pixels.
/// @param min_second Minimum size of the second (right/bottom) pane in pixels.
void vg_splitpane_set_min_sizes(vg_splitpane_t *split, float min_first, float min_second);

/// @brief Get the container widget for the first (left or top) pane.
/// @param split Split pane widget.
/// @return First pane container widget.
vg_widget_t *vg_splitpane_get_first(vg_splitpane_t *split);

/// @brief Get the container widget for the second (right or bottom) pane.
/// @param split Split pane widget.
/// @return Second pane container widget.
vg_widget_t *vg_splitpane_get_second(vg_splitpane_t *split);

//=============================================================================
// OutputPane Widget (Terminal-like output)
//=============================================================================

/// @brief ANSI color codes
typedef enum vg_ansi_color {
    VG_ANSI_DEFAULT = 0,
    VG_ANSI_BLACK = 30,
    VG_ANSI_RED,
    VG_ANSI_GREEN,
    VG_ANSI_YELLOW,
    VG_ANSI_BLUE,
    VG_ANSI_MAGENTA,
    VG_ANSI_CYAN,
    VG_ANSI_WHITE,
    VG_ANSI_BRIGHT_BLACK = 90,
    VG_ANSI_BRIGHT_RED,
    VG_ANSI_BRIGHT_GREEN,
    VG_ANSI_BRIGHT_YELLOW,
    VG_ANSI_BRIGHT_BLUE,
    VG_ANSI_BRIGHT_MAGENTA,
    VG_ANSI_BRIGHT_CYAN,
    VG_ANSI_BRIGHT_WHITE
} vg_ansi_color_t;

/// @brief Styled text segment
typedef struct vg_styled_segment {
    char *text;        ///< Segment text
    uint32_t fg_color; ///< Foreground color
    uint32_t bg_color; ///< Background color
    bool bold;         ///< Bold text
    bool italic;       ///< Italic text
    bool underline;    ///< Underlined text
} vg_styled_segment_t;

/// @brief Output line
typedef struct vg_output_line {
    vg_styled_segment_t *segments; ///< Styled segments
    size_t segment_count;          ///< Number of segments
    size_t segment_capacity;       ///< Capacity
    uint64_t timestamp;            ///< When line was added
} vg_output_line_t;

/// @brief OutputPane widget structure
///
/// @details `max_lines` is clamped to at least one line. `append_line` writes
///          exactly one logical line and reuses the trailing empty line left by
///          previous appends, so repeated calls do not introduce blank spacer
///          lines. Clearing the pane also clears any active selection.
typedef struct vg_outputpane {
    vg_widget_t base;

    // Lines
    vg_output_line_t *lines; ///< Array of output lines
    size_t line_count;
    size_t line_capacity;
    size_t max_lines; ///< Ring buffer limit (default: 10000)

    // Scrolling
    float scroll_y;     ///< Vertical scroll position
    bool auto_scroll;   ///< Scroll to bottom on new output
    bool scroll_locked; ///< User scrolled up

    // Selection
    bool has_selection;
    uint32_t sel_start_line, sel_start_col;
    uint32_t sel_end_line, sel_end_col;

    // Styling
    float line_height; ///< Height per line
    vg_font_t *font;   ///< Monospace font
    float font_size;
    uint32_t bg_color;
    uint32_t default_fg;

    // ANSI parser state
    uint32_t current_fg;
    uint32_t current_bg;
    bool ansi_bold;
    bool in_escape;
    char escape_buf[32];
    int escape_len;

    // Callbacks
    void (*on_line_click)(struct vg_outputpane *pane, int line, int col, void *user_data);
    void *user_data;
} vg_outputpane_t;

/// @brief Create a new output pane (not yet attached to a parent).
/// @return New output pane or NULL on failure.
vg_outputpane_t *vg_outputpane_create(void);

/// @brief Destroy an output pane and free all line/segment data.
/// @param pane Output pane to destroy (may be NULL).
void vg_outputpane_destroy(vg_outputpane_t *pane);

/// @brief Append text to the output pane, parsing embedded ANSI escape codes.
/// @param pane Output pane.
/// @param text Null-terminated text to append (may contain ANSI sequences).
void vg_outputpane_append(vg_outputpane_t *pane, const char *text);

/// @brief Append a complete line (a newline is appended automatically).
/// @param pane Output pane.
/// @param text Null-terminated line text (ANSI codes are parsed).
void vg_outputpane_append_line(vg_outputpane_t *pane, const char *text);

/// @brief Append text with explicit colour and style attributes.
/// @param pane Output pane.
/// @param text Null-terminated text to append.
/// @param fg   Foreground ARGB colour (0 = use current foreground).
/// @param bg   Background ARGB colour (0 = use current background).
/// @param bold true to render the segment in bold.
void vg_outputpane_append_styled(
    vg_outputpane_t *pane, const char *text, uint32_t fg, uint32_t bg, bool bold);

/// @brief Remove all output lines and reset the ANSI parser state.
/// @param pane Output pane.
void vg_outputpane_clear(vg_outputpane_t *pane);

/// @brief Scroll to the last line of output.
/// @param pane Output pane.
void vg_outputpane_scroll_to_bottom(vg_outputpane_t *pane);

/// @brief Scroll to the first line of output.
/// @param pane Output pane.
void vg_outputpane_scroll_to_top(vg_outputpane_t *pane);

/// @brief Control whether the pane scrolls to the bottom on each new line.
/// @param pane        Output pane.
/// @param auto_scroll true to keep the view pinned to the newest output.
void vg_outputpane_set_auto_scroll(vg_outputpane_t *pane, bool auto_scroll);

/// @brief Return the currently selected text as a newly allocated string.
/// @param pane Output pane.
/// @return Caller-owned null-terminated string, or NULL if nothing is selected.
char *vg_outputpane_get_selection(vg_outputpane_t *pane);

/// @brief Select all text in the output pane.
/// @param pane Output pane.
void vg_outputpane_select_all(vg_outputpane_t *pane);

/// @brief Set the ring-buffer line limit; oldest lines are discarded when exceeded.
/// @param pane Output pane.
/// @param max  Maximum number of lines to retain (0 = unlimited).
void vg_outputpane_set_max_lines(vg_outputpane_t *pane, size_t max);

/// @brief Set the monospace font used for output text.
/// @param pane Output pane.
/// @param font Font handle (should be a monospace face).
/// @param size Font size in pixels.
void vg_outputpane_set_font(vg_outputpane_t *pane, vg_font_t *font, float size);

//=============================================================================
// Breadcrumb Widget
//=============================================================================

/// @brief Breadcrumb dropdown item
typedef struct vg_breadcrumb_dropdown {
    char *label; ///< Dropdown item label
    void *data;  ///< User data
} vg_breadcrumb_dropdown_t;

/// @brief Breadcrumb item
typedef struct vg_breadcrumb_item {
    char *label;         ///< Item label
    char *tooltip;       ///< Tooltip text
    vg_icon_t icon;      ///< Optional icon
    void *user_data;     ///< User data
    bool owns_user_data; ///< Free user_data when the item is destroyed

    // Dropdown items
    vg_breadcrumb_dropdown_t *dropdown_items;
    size_t dropdown_count;
    size_t dropdown_capacity;
} vg_breadcrumb_item_t;

/// @brief Breadcrumb widget structure
typedef struct vg_breadcrumb {
    vg_widget_t base;

    // Items
    vg_breadcrumb_item_t *items; ///< Breadcrumb items
    size_t item_count;
    size_t item_capacity;

    // Styling
    char *separator;            ///< Separator string (default: ">")
    uint32_t item_padding;      ///< Padding around items
    uint32_t separator_padding; ///< Padding around separator
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t hover_bg;
    uint32_t separator_color;

    // Font
    vg_font_t *font;
    float font_size;

    // State
    int hovered_index;    ///< Hovered item index
    bool dropdown_open;   ///< Is dropdown open
    int dropdown_index;   ///< Which item's dropdown is open
    int dropdown_hovered; ///< Hovered dropdown item
    int max_items; ///< Maximum items to display (0 = unlimited, oldest removed when exceeded)

    // Callbacks
    void (*on_click)(struct vg_breadcrumb *bc, int index, void *user_data);
    void (*on_dropdown_select)(struct vg_breadcrumb *bc,
                               int crumb_index,
                               int dropdown_index,
                               void *user_data);
    void *user_data;
} vg_breadcrumb_t;

/// @brief Create a new breadcrumb widget (not yet attached to a parent).
/// @return New breadcrumb or NULL on failure.
vg_breadcrumb_t *vg_breadcrumb_create(void);

/// @brief Destroy a breadcrumb widget and free all item data.
/// @param bc Breadcrumb widget to destroy (may be NULL).
void vg_breadcrumb_destroy(vg_breadcrumb_t *bc);

/// @brief Append a new crumb to the right end of the breadcrumb trail.
/// @param bc    Breadcrumb widget.
/// @param label Display label (copied internally).
/// @param data  Caller-owned data associated with this crumb.
void vg_breadcrumb_push(vg_breadcrumb_t *bc, const char *label, void *data);

/// @brief Remove and free the rightmost crumb.
/// @param bc Breadcrumb widget.
void vg_breadcrumb_pop(vg_breadcrumb_t *bc);

/// @brief Remove and free all crumbs.
/// @param bc Breadcrumb widget.
void vg_breadcrumb_clear(vg_breadcrumb_t *bc);

/// @brief Add a dropdown entry to an existing breadcrumb item.
/// @param item  The parent breadcrumb item.
/// @param label Dropdown item label (copied internally).
/// @param data  Caller-owned data for the dropdown entry.
void vg_breadcrumb_item_add_dropdown(vg_breadcrumb_item_t *item, const char *label, void *data);

/// @brief Set the separator string drawn between crumbs.
/// @param bc  Breadcrumb widget.
/// @param sep Separator string (copied internally; default is ">").
void vg_breadcrumb_set_separator(vg_breadcrumb_t *bc, const char *sep);

/// @brief Set the callback fired when a crumb is clicked.
/// @param bc        Breadcrumb widget.
/// @param callback  Handler called with the breadcrumb, zero-based crumb index, and user_data.
/// @param user_data User data passed to the handler.
void vg_breadcrumb_set_on_click(vg_breadcrumb_t *bc,
                                void (*callback)(vg_breadcrumb_t *, int, void *),
                                void *user_data);

/// @brief Set the font used to render crumb labels.
/// @param bc   Breadcrumb widget.
/// @param font Font handle.
/// @param size Font size in pixels.
void vg_breadcrumb_set_font(vg_breadcrumb_t *bc, vg_font_t *font, float size);

/// @brief Set the maximum number of crumbs to display (oldest are removed when exceeded).
/// @param bc  Breadcrumb widget.
/// @param max Maximum crumb count (0 = unlimited).
void vg_breadcrumb_set_max_items(vg_breadcrumb_t *bc, int max);

#ifdef __cplusplus
}
#endif
