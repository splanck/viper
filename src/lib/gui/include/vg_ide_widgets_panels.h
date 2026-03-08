//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/include/vg_ide_widgets_panels.h
// Purpose: SplitPane, TabBar, OutputPane, and Breadcrumb widget declarations.
// Key invariants:
//   - All create functions return NULL on allocation failure.
//   - String parameters are copied internally unless documented otherwise.
// Ownership/Lifetime:
//   - Widgets are owned by their parent in the widget tree.
// Links: vg_ide_widgets_common.h, vg_widget.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vg_ide_widgets_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // TabBar Widget
    //=============================================================================

    /// @brief Tab structure
    typedef struct vg_tab
    {
        const char *title;   ///< Tab title (owned)
        const char *tooltip; ///< Tab tooltip (owned)
        void *user_data;     ///< User data
        bool closable;       ///< Can tab be closed
        bool modified;       ///< Show modified indicator
        struct vg_tab *next;
        struct vg_tab *prev;
    } vg_tab_t;

    /// @brief Tab callbacks
    typedef void (*vg_tab_select_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
    typedef bool (*vg_tab_close_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
    typedef void (*vg_tab_reorder_callback_t)(vg_widget_t *tabbar,
                                              vg_tab_t *tab,
                                              int new_index,
                                              void *user_data);

    /// @brief TabBar widget structure
    typedef struct vg_tabbar
    {
        vg_widget_t base;

        vg_tab_t *first_tab;  ///< First tab
        vg_tab_t *last_tab;   ///< Last tab
        vg_tab_t *active_tab; ///< Currently active tab
        int tab_count;        ///< Number of tabs

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
        vg_tab_t *hovered_tab;     ///< Currently hovered tab
        bool close_button_hovered; ///< Is close button hovered
        bool dragging;             ///< Is dragging a tab
        vg_tab_t *drag_tab;        ///< Tab being dragged
        float drag_x;              ///< Drag position

        // Per-frame tracking for Zia runtime
        vg_tab_t *prev_active_tab;   ///< Previous active tab (for change detection)
        vg_tab_t *close_clicked_tab; ///< Tab whose close button was clicked (cleared on read)
        bool auto_close;             ///< Auto-remove tab on close click (default true)
    } vg_tabbar_t;

    /// @brief Create a new tab bar widget
    vg_tabbar_t *vg_tabbar_create(vg_widget_t *parent);

    /// @brief Add a tab
    vg_tab_t *vg_tabbar_add_tab(vg_tabbar_t *tabbar, const char *title, bool closable);

    /// @brief Remove a tab
    void vg_tabbar_remove_tab(vg_tabbar_t *tabbar, vg_tab_t *tab);

    /// @brief Set active tab
    void vg_tabbar_set_active(vg_tabbar_t *tabbar, vg_tab_t *tab);

    /// @brief Get active tab
    vg_tab_t *vg_tabbar_get_active(vg_tabbar_t *tabbar);

    /// @brief Get the index of a tab in the tab bar
    int vg_tabbar_get_tab_index(vg_tabbar_t *tabbar, vg_tab_t *tab);

    /// @brief Get a tab by index
    vg_tab_t *vg_tabbar_get_tab_at(vg_tabbar_t *tabbar, int index);

    /// @brief Set tab title
    void vg_tab_set_title(vg_tab_t *tab, const char *title);

    /// @brief Set tab modified state
    void vg_tab_set_modified(vg_tab_t *tab, bool modified);

    /// @brief Set tab user data
    void vg_tab_set_data(vg_tab_t *tab, void *data);

    /// @brief Set font for tab bar
    void vg_tabbar_set_font(vg_tabbar_t *tabbar, vg_font_t *font, float size);

    /// @brief Set tab selection callback
    void vg_tabbar_set_on_select(vg_tabbar_t *tabbar,
                                 vg_tab_select_callback_t callback,
                                 void *user_data);

    /// @brief Set tab close callback
    void vg_tabbar_set_on_close(vg_tabbar_t *tabbar,
                                vg_tab_close_callback_t callback,
                                void *user_data);

    /// @brief Set tab reorder callback
    void vg_tabbar_set_on_reorder(vg_tabbar_t *tabbar,
                                  vg_tab_reorder_callback_t callback,
                                  void *user_data);

    //=============================================================================
    // SplitPane Widget
    //=============================================================================

    /// @brief Split direction
    typedef enum vg_split_direction
    {
        VG_SPLIT_HORIZONTAL, ///< Left/Right split
        VG_SPLIT_VERTICAL    ///< Top/Bottom split
    } vg_split_direction_t;

    /// @brief SplitPane widget structure
    typedef struct vg_splitpane
    {
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

    /// @brief Create a new split pane widget
    vg_splitpane_t *vg_splitpane_create(vg_widget_t *parent, vg_split_direction_t direction);

    /// @brief Set split position
    void vg_splitpane_set_position(vg_splitpane_t *split, float position);

    /// @brief Get split position
    float vg_splitpane_get_position(vg_splitpane_t *split);

    /// @brief Set minimum pane sizes
    void vg_splitpane_set_min_sizes(vg_splitpane_t *split, float min_first, float min_second);

    /// @brief Get first pane (for adding content)
    vg_widget_t *vg_splitpane_get_first(vg_splitpane_t *split);

    /// @brief Get second pane (for adding content)
    vg_widget_t *vg_splitpane_get_second(vg_splitpane_t *split);

    //=============================================================================
    // OutputPane Widget (Terminal-like output)
    //=============================================================================

    /// @brief ANSI color codes
    typedef enum vg_ansi_color
    {
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
    typedef struct vg_styled_segment
    {
        char *text;        ///< Segment text
        uint32_t fg_color; ///< Foreground color
        uint32_t bg_color; ///< Background color
        bool bold;         ///< Bold text
        bool italic;       ///< Italic text
        bool underline;    ///< Underlined text
    } vg_styled_segment_t;

    /// @brief Output line
    typedef struct vg_output_line
    {
        vg_styled_segment_t *segments; ///< Styled segments
        size_t segment_count;          ///< Number of segments
        size_t segment_capacity;       ///< Capacity
        uint64_t timestamp;            ///< When line was added
    } vg_output_line_t;

    /// @brief OutputPane widget structure
    typedef struct vg_outputpane
    {
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

    /// @brief Create a new output pane
    vg_outputpane_t *vg_outputpane_create(void);

    /// @brief Destroy an output pane
    void vg_outputpane_destroy(vg_outputpane_t *pane);

    /// @brief Append text (handles ANSI codes)
    void vg_outputpane_append(vg_outputpane_t *pane, const char *text);

    /// @brief Append a complete line
    void vg_outputpane_append_line(vg_outputpane_t *pane, const char *text);

    /// @brief Append styled text
    void vg_outputpane_append_styled(
        vg_outputpane_t *pane, const char *text, uint32_t fg, uint32_t bg, bool bold);

    /// @brief Clear all output
    void vg_outputpane_clear(vg_outputpane_t *pane);

    /// @brief Scroll to bottom
    void vg_outputpane_scroll_to_bottom(vg_outputpane_t *pane);

    /// @brief Scroll to top
    void vg_outputpane_scroll_to_top(vg_outputpane_t *pane);

    /// @brief Set auto-scroll behavior
    void vg_outputpane_set_auto_scroll(vg_outputpane_t *pane, bool auto_scroll);

    /// @brief Get selected text (caller must free)
    char *vg_outputpane_get_selection(vg_outputpane_t *pane);

    /// @brief Select all text
    void vg_outputpane_select_all(vg_outputpane_t *pane);

    /// @brief Set maximum lines
    void vg_outputpane_set_max_lines(vg_outputpane_t *pane, size_t max);

    /// @brief Set font
    void vg_outputpane_set_font(vg_outputpane_t *pane, vg_font_t *font, float size);

    //=============================================================================
    // Breadcrumb Widget
    //=============================================================================

    /// @brief Breadcrumb dropdown item
    typedef struct vg_breadcrumb_dropdown
    {
        char *label; ///< Dropdown item label
        void *data;  ///< User data
    } vg_breadcrumb_dropdown_t;

    /// @brief Breadcrumb item
    typedef struct vg_breadcrumb_item
    {
        char *label;     ///< Item label
        char *tooltip;   ///< Tooltip text
        vg_icon_t icon;  ///< Optional icon
        void *user_data; ///< User data

        // Dropdown items
        vg_breadcrumb_dropdown_t *dropdown_items;
        size_t dropdown_count;
        size_t dropdown_capacity;
    } vg_breadcrumb_item_t;

    /// @brief Breadcrumb widget structure
    typedef struct vg_breadcrumb
    {
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

    /// @brief Create a new breadcrumb widget
    vg_breadcrumb_t *vg_breadcrumb_create(void);

    /// @brief Destroy a breadcrumb widget
    void vg_breadcrumb_destroy(vg_breadcrumb_t *bc);

    /// @brief Push a new item onto the breadcrumb
    void vg_breadcrumb_push(vg_breadcrumb_t *bc, const char *label, void *data);

    /// @brief Pop the last item from the breadcrumb
    void vg_breadcrumb_pop(vg_breadcrumb_t *bc);

    /// @brief Clear all items
    void vg_breadcrumb_clear(vg_breadcrumb_t *bc);

    /// @brief Add dropdown item to a breadcrumb item
    void vg_breadcrumb_item_add_dropdown(vg_breadcrumb_item_t *item, const char *label, void *data);

    /// @brief Set separator string
    void vg_breadcrumb_set_separator(vg_breadcrumb_t *bc, const char *sep);

    /// @brief Set click callback
    void vg_breadcrumb_set_on_click(vg_breadcrumb_t *bc,
                                    void (*callback)(vg_breadcrumb_t *, int, void *),
                                    void *user_data);

    /// @brief Set font
    void vg_breadcrumb_set_font(vg_breadcrumb_t *bc, vg_font_t *font, float size);

    /// @brief Set maximum number of breadcrumb items.
    void vg_breadcrumb_set_max_items(vg_breadcrumb_t *bc, int max);

#ifdef __cplusplus
}
#endif
