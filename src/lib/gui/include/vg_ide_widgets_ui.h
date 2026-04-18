//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/include/vg_ide_widgets_ui.h
// Purpose: StatusBar, Toolbar, CommandPalette, Notification, Tooltip, and
//          FloatingPanel widget declarations.
// Key invariants:
//   - All create functions return NULL on allocation failure.
//   - String parameters are copied internally unless documented otherwise.
// Ownership/Lifetime:
//   - Widgets are owned by their parent in the widget tree.
//   - CommandPalette and Tooltip may be parentless and must be explicitly
//     destroyed.
// Links: vg_ide_widgets_common.h, vg_widget.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vg_ide_widgets_common.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// StatusBar Widget
//=============================================================================

/// @brief StatusBar item types
typedef enum vg_statusbar_item_type {
    VG_STATUSBAR_ITEM_TEXT,      ///< Static text label
    VG_STATUSBAR_ITEM_BUTTON,    ///< Clickable button
    VG_STATUSBAR_ITEM_PROGRESS,  ///< Progress indicator
    VG_STATUSBAR_ITEM_SEPARATOR, ///< Vertical separator line
    VG_STATUSBAR_ITEM_SPACER     ///< Flexible spacer
} vg_statusbar_item_type_t;

/// @brief StatusBar zone for item placement
typedef enum vg_statusbar_zone {
    VG_STATUSBAR_ZONE_LEFT,   ///< Left-aligned zone
    VG_STATUSBAR_ZONE_CENTER, ///< Center-aligned zone
    VG_STATUSBAR_ZONE_RIGHT   ///< Right-aligned zone
} vg_statusbar_zone_t;

/// @brief StatusBar item structure
typedef struct vg_statusbar_item {
    vg_statusbar_item_type_t type; ///< Item type
    struct vg_statusbar *owner;    ///< Owning status bar for invalidation
    char *text;                    ///< Item text (owned)
    char *tooltip;                 ///< Tooltip text (owned)
    float min_width;               ///< Minimum width (0 = auto)
    float max_width;               ///< Maximum width (0 = unlimited)
    bool visible;                  ///< Is item visible
    float progress;                ///< Progress value (0-1) for progress items
    void *user_data;               ///< User data
    void (*on_click)(struct vg_statusbar_item *, void *); ///< Click callback for buttons
} vg_statusbar_item_t;

/// @brief StatusBar widget structure
typedef struct vg_statusbar {
    vg_widget_t base;

    // Items by zone
    vg_statusbar_item_t **left_items;
    size_t left_count;
    size_t left_capacity;

    vg_statusbar_item_t **center_items;
    size_t center_count;
    size_t center_capacity;

    vg_statusbar_item_t **right_items;
    size_t right_count;
    size_t right_capacity;

    // Styling
    int height;            ///< StatusBar height
    float item_padding;    ///< Padding between items
    float separator_width; ///< Separator line width

    // Font
    vg_font_t *font; ///< Font for text
    float font_size; ///< Font size

    // Colors
    uint32_t bg_color;     ///< Background color
    uint32_t text_color;   ///< Text color
    uint32_t hover_color;  ///< Hover background
    uint32_t border_color; ///< Top border color

    // State
    vg_statusbar_item_t *hovered_item; ///< Currently hovered item
} vg_statusbar_t;

/// @brief Create a new status bar widget
vg_statusbar_t *vg_statusbar_create(vg_widget_t *parent);

/// @brief Add a text item to the status bar
vg_statusbar_item_t *vg_statusbar_add_text(vg_statusbar_t *sb,
                                           vg_statusbar_zone_t zone,
                                           const char *text);

/// @brief Add a button item to the status bar
vg_statusbar_item_t *vg_statusbar_add_button(vg_statusbar_t *sb,
                                             vg_statusbar_zone_t zone,
                                             const char *text,
                                             void (*on_click)(vg_statusbar_item_t *, void *),
                                             void *user_data);

/// @brief Add a progress indicator to the status bar
vg_statusbar_item_t *vg_statusbar_add_progress(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

/// @brief Add a separator to the status bar
vg_statusbar_item_t *vg_statusbar_add_separator(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

/// @brief Add a spacer to the status bar
vg_statusbar_item_t *vg_statusbar_add_spacer(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

/// @brief Remove an item from the status bar
void vg_statusbar_remove_item(vg_statusbar_t *sb, vg_statusbar_item_t *item);

/// @brief Clear all items in a zone
void vg_statusbar_clear_zone(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

/// @brief Set item text
void vg_statusbar_item_set_text(vg_statusbar_item_t *item, const char *text);

/// @brief Set item tooltip
void vg_statusbar_item_set_tooltip(vg_statusbar_item_t *item, const char *tooltip);

/// @brief Set progress value (for progress items)
void vg_statusbar_item_set_progress(vg_statusbar_item_t *item, float progress);

/// @brief Set item visibility
void vg_statusbar_item_set_visible(vg_statusbar_item_t *item, bool visible);

/// @brief Set font for status bar
void vg_statusbar_set_font(vg_statusbar_t *sb, vg_font_t *font, float size);

/// @brief Convenience: set cursor position display (Ln X, Col Y)
void vg_statusbar_set_cursor_position(vg_statusbar_t *sb, int line, int col);

//=============================================================================
// Toolbar Widget
//=============================================================================

/// @brief Toolbar item types
typedef enum vg_toolbar_item_type {
    VG_TOOLBAR_ITEM_BUTTON,    ///< Standard button
    VG_TOOLBAR_ITEM_TOGGLE,    ///< Toggle button (checkable)
    VG_TOOLBAR_ITEM_DROPDOWN,  ///< Button with dropdown menu
    VG_TOOLBAR_ITEM_SEPARATOR, ///< Vertical line separator
    VG_TOOLBAR_ITEM_SPACER,    ///< Flexible spacer
    VG_TOOLBAR_ITEM_WIDGET     ///< Custom embedded widget
} vg_toolbar_item_type_t;

/// @brief Toolbar orientation
typedef enum vg_toolbar_orientation {
    VG_TOOLBAR_HORIZONTAL, ///< Horizontal toolbar
    VG_TOOLBAR_VERTICAL    ///< Vertical toolbar
} vg_toolbar_orientation_t;

/// @brief Toolbar icon size presets
typedef enum vg_toolbar_icon_size {
    VG_TOOLBAR_ICONS_SMALL,  ///< 16x16 icons
    VG_TOOLBAR_ICONS_MEDIUM, ///< 24x24 icons
    VG_TOOLBAR_ICONS_LARGE   ///< 32x32 icons
} vg_toolbar_icon_size_t;

/// @brief Toolbar item structure
typedef struct vg_toolbar_item {
    vg_toolbar_item_type_t type; ///< Item type
    struct vg_toolbar *owner;    ///< Owning toolbar for invalidation/popup handling
    char *id;                    ///< Unique identifier
    char *label;                 ///< Text label (optional)
    char *tooltip;               ///< Hover tooltip
    vg_icon_t icon;              ///< Icon specification
    bool enabled;                ///< Enabled state
    bool checked;                ///< For toggle items
    bool show_label;             ///< Show text label
    bool was_clicked;            ///< Set true when item is clicked (cleared on read)

    struct vg_menu *dropdown_menu; ///< Dropdown menu (for DROPDOWN type)
    vg_widget_t *custom_widget;    ///< Custom widget (for WIDGET type)

    void *user_data;                                           ///< User data
    void (*on_click)(struct vg_toolbar_item *, void *);        ///< Click callback
    void (*on_toggle)(struct vg_toolbar_item *, bool, void *); ///< Toggle callback
} vg_toolbar_item_t;

/// @brief Toolbar widget structure
typedef struct vg_toolbar {
    vg_widget_t base;

    vg_toolbar_item_t **items; ///< Array of items
    size_t item_count;         ///< Number of items
    size_t item_capacity;      ///< Allocated capacity

    // Configuration
    vg_toolbar_orientation_t orientation; ///< Orientation
    vg_toolbar_icon_size_t icon_size;     ///< Icon size preset
    uint32_t item_padding;                ///< Padding around items
    uint32_t item_spacing;                ///< Space between items
    bool show_labels;                     ///< Global label visibility
    bool overflow_menu;                   ///< Show overflow items in dropdown

    // Font
    vg_font_t *font; ///< Font for text
    float font_size; ///< Font size

    // Colors
    uint32_t bg_color;       ///< Background color
    uint32_t hover_color;    ///< Hover color
    uint32_t active_color;   ///< Active/pressed color
    uint32_t text_color;     ///< Text color
    uint32_t disabled_color; ///< Disabled text color

    // State
    vg_toolbar_item_t *hovered_item; ///< Currently hovered item
    vg_toolbar_item_t *pressed_item; ///< Currently pressed item
    int overflow_start_index;        ///< First item in overflow (-1 if none)
    bool overflow_button_hovered;    ///< Hover state for overflow button
    vg_contextmenu_t *overflow_popup; ///< Popup for overflowed items
    bool overflow_popup_dirty;        ///< Rebuild popup contents before next show
    vg_contextmenu_t *dropdown_popup; ///< Popup sourced from a dropdown menu item
    vg_toolbar_item_t *dropdown_item; ///< Dropdown item currently showing a popup
} vg_toolbar_t;

/// @brief Create a new toolbar widget
vg_toolbar_t *vg_toolbar_create(vg_widget_t *parent, vg_toolbar_orientation_t orientation);

/// @brief Add a button to the toolbar
vg_toolbar_item_t *vg_toolbar_add_button(vg_toolbar_t *tb,
                                         const char *id,
                                         const char *label,
                                         vg_icon_t icon,
                                         void (*on_click)(vg_toolbar_item_t *, void *),
                                         void *user_data);

/// @brief Add a toggle button to the toolbar
vg_toolbar_item_t *vg_toolbar_add_toggle(vg_toolbar_t *tb,
                                         const char *id,
                                         const char *label,
                                         vg_icon_t icon,
                                         bool initial_checked,
                                         void (*on_toggle)(vg_toolbar_item_t *, bool, void *),
                                         void *user_data);

/// @brief Add a dropdown button to the toolbar
vg_toolbar_item_t *vg_toolbar_add_dropdown(
    vg_toolbar_t *tb, const char *id, const char *label, vg_icon_t icon, struct vg_menu *menu);

/// @brief Add a separator to the toolbar
vg_toolbar_item_t *vg_toolbar_add_separator(vg_toolbar_t *tb);

/// @brief Add a spacer to the toolbar
vg_toolbar_item_t *vg_toolbar_add_spacer(vg_toolbar_t *tb);

/// @brief Add a custom widget to the toolbar
vg_toolbar_item_t *vg_toolbar_add_widget(vg_toolbar_t *tb, const char *id, vg_widget_t *widget);

/// @brief Remove an item from the toolbar by ID
void vg_toolbar_remove_item(vg_toolbar_t *tb, const char *id);

/// @brief Get an item by ID
vg_toolbar_item_t *vg_toolbar_get_item(vg_toolbar_t *tb, const char *id);

/// @brief Set item enabled state
void vg_toolbar_item_set_enabled(vg_toolbar_item_t *item, bool enabled);

/// @brief Set item checked state (for toggle items)
void vg_toolbar_item_set_checked(vg_toolbar_item_t *item, bool checked);

/// @brief Set item tooltip
void vg_toolbar_item_set_tooltip(vg_toolbar_item_t *item, const char *tooltip);

/// @brief Set item label text
void vg_toolbar_item_set_text(vg_toolbar_item_t *item, const char *text);

/// @brief Set item icon
void vg_toolbar_item_set_icon(vg_toolbar_item_t *item, vg_icon_t icon);

/// @brief Set icon size for toolbar
void vg_toolbar_set_icon_size(vg_toolbar_t *tb, vg_toolbar_icon_size_t size);

/// @brief Set whether labels are shown
void vg_toolbar_set_show_labels(vg_toolbar_t *tb, bool show);

/// @brief Set font for toolbar
void vg_toolbar_set_font(vg_toolbar_t *tb, vg_font_t *font, float size);

//=============================================================================
// Tooltip Widget
//=============================================================================

/// @brief Tooltip position mode
typedef enum vg_tooltip_position {
    VG_TOOLTIP_FOLLOW_CURSOR, ///< Follow mouse cursor
    VG_TOOLTIP_ANCHOR_WIDGET  ///< Anchor to specific widget
} vg_tooltip_position_t;

/// @brief Tooltip widget structure
typedef struct vg_tooltip {
    vg_widget_t base;

    // Content
    char *text;           ///< Plain text content
    vg_widget_t *content; ///< Rich content (alternative)

    // Timing
    uint32_t show_delay_ms; ///< Delay before showing (default: 500)
    uint32_t hide_delay_ms; ///< Delay before hiding on leave (default: 100)
    uint32_t duration_ms;   ///< Auto-hide after (0 = stay until leave)

    // Positioning
    vg_tooltip_position_t position_mode;
    int offset_x;               ///< X offset from anchor
    int offset_y;               ///< Y offset from anchor
    vg_widget_t *anchor_widget; ///< Widget to anchor to

    // Styling
    uint32_t max_width;     ///< Max width before wrapping (default: 300)
    uint32_t padding;       ///< Internal padding
    uint32_t corner_radius; ///< Corner radius
    uint32_t bg_color;      ///< Background color
    uint32_t text_color;    ///< Text color
    uint32_t border_color;  ///< Border color

    // Font
    vg_font_t *font;
    float font_size;

    // State
    bool is_visible;     ///< Currently visible
    uint64_t show_timer; ///< Timer for show delay
    uint64_t hide_timer; ///< Timer for hide delay
} vg_tooltip_t;

/// @brief Tooltip manager (singleton pattern)
typedef struct vg_tooltip_manager {
    vg_tooltip_t *active_tooltip; ///< Currently showing tooltip
    vg_widget_t *hovered_widget;  ///< Widget mouse is over
    uint64_t hover_start_time;    ///< When hover started
    bool pending_show;            ///< Tooltip pending display
    int cursor_x;                 ///< Cursor position
    int cursor_y;
} vg_tooltip_manager_t;

/// @brief Create a new tooltip widget
vg_tooltip_t *vg_tooltip_create(void);

/// @brief Destroy a tooltip widget
void vg_tooltip_destroy(vg_tooltip_t *tooltip);

/// @brief Set tooltip text
void vg_tooltip_set_text(vg_tooltip_t *tooltip, const char *text);

/// @brief Show tooltip at position
void vg_tooltip_show_at(vg_tooltip_t *tooltip, int x, int y);

/// @brief Hide tooltip
void vg_tooltip_hide(vg_tooltip_t *tooltip);

/// @brief Set tooltip anchor widget
void vg_tooltip_set_anchor(vg_tooltip_t *tooltip, vg_widget_t *anchor);

/// @brief Set tooltip timing
void vg_tooltip_set_timing(vg_tooltip_t *tooltip,
                           uint32_t show_delay_ms,
                           uint32_t hide_delay_ms,
                           uint32_t duration_ms);

// --- Tooltip Manager ---

/// @brief Get global tooltip manager
vg_tooltip_manager_t *vg_tooltip_manager_get(void);

/// @brief Update tooltip manager (call each frame)
void vg_tooltip_manager_update(vg_tooltip_manager_t *mgr, uint64_t now_ms);

/// @brief Notify manager of hover
void vg_tooltip_manager_on_hover(vg_tooltip_manager_t *mgr, vg_widget_t *widget, int x, int y);

/// @brief Notify manager of leave
void vg_tooltip_manager_on_leave(vg_tooltip_manager_t *mgr);

/// @brief Notify manager that a widget is being destroyed.
///
/// Clears any references the manager holds to the widget so subsequent
/// hover/leave callbacks do not dereference freed memory. Safe to call from
/// vg_widget_destroy. Operates on the global manager singleton.
void vg_tooltip_manager_widget_destroyed(vg_widget_t *widget);

/// @brief Notify manager that a widget (or one of its descendants) became hidden/disabled.
///
/// Hides the active tooltip when its hovered widget or anchor lives inside the
/// subtree being hidden. Safe to call from visibility/enabled-state setters.
void vg_tooltip_manager_widget_hidden(vg_widget_t *widget);

/// @brief Set tooltip for a widget
void vg_widget_set_tooltip_text(vg_widget_t *widget, const char *text);

//=============================================================================
// CommandPalette Widget
//=============================================================================

/// @brief Command structure
typedef struct vg_command {
    char *id;                                                ///< Unique ID
    char *label;                                             ///< Display text
    char *description;                                       ///< Optional description
    char *shortcut;                                          ///< Keyboard shortcut display
    char *category;                                          ///< Category for grouping
    vg_icon_t icon;                                          ///< Command icon
    bool enabled;                                            ///< Is command enabled
    void *user_data;                                         ///< User data
    void (*action)(struct vg_command *cmd, void *user_data); ///< Action callback
} vg_command_t;

/// @brief CommandPalette widget structure
typedef struct vg_commandpalette {
    vg_widget_t base;

    // Commands
    vg_command_t **commands; ///< All registered commands
    size_t command_count;
    size_t command_capacity;

    // Filtered results
    vg_command_t **filtered; ///< Filtered command list
    size_t filtered_count;
    size_t filtered_capacity;

    // Search state
    char *placeholder_text; ///< Placeholder shown when query is empty
    char *current_query;    ///< Current search query (UTF-8)

    // State
    bool is_visible;    ///< Is palette visible
    int selected_index; ///< Selected result index
    int hovered_index;  ///< Hovered result index

    // Appearance
    uint32_t item_height; ///< Height of each result item
    uint32_t max_visible; ///< Max visible results
    float width;          ///< Palette width
    uint32_t bg_color;
    uint32_t selected_bg;
    uint32_t text_color;
    uint32_t shortcut_color;

    // Font
    vg_font_t *font;
    float font_size;

    // Callbacks
    void (*on_execute)(struct vg_commandpalette *palette, vg_command_t *cmd, void *user_data);
    void (*on_dismiss)(struct vg_commandpalette *palette, void *user_data);
    void *user_data;
} vg_commandpalette_t;

/// @brief Create a new command palette
vg_commandpalette_t *vg_commandpalette_create(void);

/// @brief Destroy a command palette
void vg_commandpalette_destroy(vg_commandpalette_t *palette);

/// @brief Add a command
vg_command_t *vg_commandpalette_add_command(vg_commandpalette_t *palette,
                                            const char *id,
                                            const char *label,
                                            const char *shortcut,
                                            void (*action)(vg_command_t *, void *),
                                            void *user_data);

/// @brief Remove a command by ID
void vg_commandpalette_remove_command(vg_commandpalette_t *palette, const char *id);

/// @brief Get command by ID
vg_command_t *vg_commandpalette_get_command(vg_commandpalette_t *palette, const char *id);

/// @brief Remove all commands from the palette.
void vg_commandpalette_clear(vg_commandpalette_t *palette);

/// @brief Show command palette
void vg_commandpalette_show(vg_commandpalette_t *palette);

/// @brief Hide command palette
void vg_commandpalette_hide(vg_commandpalette_t *palette);

/// @brief Toggle command palette visibility
void vg_commandpalette_toggle(vg_commandpalette_t *palette);

/// @brief Execute selected command
void vg_commandpalette_execute_selected(vg_commandpalette_t *palette);

/// @brief Set callbacks
void vg_commandpalette_set_callbacks(vg_commandpalette_t *palette,
                                     void (*on_execute)(vg_commandpalette_t *,
                                                        vg_command_t *,
                                                        void *),
                                     void (*on_dismiss)(vg_commandpalette_t *, void *),
                                     void *user_data);

/// @brief Set font
void vg_commandpalette_set_font(vg_commandpalette_t *palette, vg_font_t *font, float size);

/// @brief Set placeholder text
void vg_commandpalette_set_placeholder(vg_commandpalette_t *palette, const char *text);

//=============================================================================
// Notification Widget
//=============================================================================

/// @brief Notification type
typedef enum vg_notification_type {
    VG_NOTIFICATION_INFO,    ///< Informational
    VG_NOTIFICATION_SUCCESS, ///< Success message
    VG_NOTIFICATION_WARNING, ///< Warning message
    VG_NOTIFICATION_ERROR    ///< Error message
} vg_notification_type_t;

/// @brief Notification position
typedef enum vg_notification_position {
    VG_NOTIFICATION_TOP_RIGHT,
    VG_NOTIFICATION_TOP_LEFT,
    VG_NOTIFICATION_BOTTOM_RIGHT,
    VG_NOTIFICATION_BOTTOM_LEFT,
    VG_NOTIFICATION_TOP_CENTER,
    VG_NOTIFICATION_BOTTOM_CENTER
} vg_notification_position_t;

/// @brief Single notification
typedef struct vg_notification {
    uint32_t id;                 ///< Unique ID
    vg_notification_type_t type; ///< Notification type
    char *title;                 ///< Title text
    char *message;               ///< Message text
    uint32_t duration_ms;        ///< Auto-dismiss duration (0 = sticky)
    uint64_t created_at;         ///< Creation timestamp

    // Action
    char *action_label; ///< Action button label
    void (*action_callback)(uint32_t id, void *user_data);
    void *action_user_data;

    // State
    float opacity;             ///< Current opacity (for animation)
    float slide_progress;      ///< Entrance / exit slide interpolation (0..1)
    uint64_t dismiss_started_at; ///< When the dismissal animation began (0 = not dismissing)
    bool dismissed;            ///< Dismissal requested / in progress
} vg_notification_t;

/// @brief Notification manager widget
typedef struct vg_notification_manager {
    vg_widget_t base;

    // Notifications
    vg_notification_t **notifications;
    size_t notification_count;
    size_t notification_capacity;

    // Positioning
    vg_notification_position_t position;

    // Styling
    uint32_t max_visible; ///< Max visible notifications
    uint32_t notification_width;
    uint32_t spacing; ///< Space between notifications
    uint32_t margin;  ///< Margin from edges
    uint32_t padding; ///< Internal padding

    // Font
    vg_font_t *font;
    float font_size;
    float title_font_size;

    // Colors per type
    uint32_t info_color;
    uint32_t success_color;
    uint32_t warning_color;
    uint32_t error_color;
    uint32_t bg_color;
    uint32_t text_color;

    // Animation
    uint32_t fade_duration_ms;
    uint32_t slide_duration_ms;

    // ID counter
    uint32_t next_id;
} vg_notification_manager_t;

/// @brief Create notification manager
vg_notification_manager_t *vg_notification_manager_create(void);

/// @brief Destroy notification manager
void vg_notification_manager_destroy(vg_notification_manager_t *mgr);

/// @brief Update animations (call each frame)
void vg_notification_manager_update(vg_notification_manager_t *mgr, uint64_t now_ms);

/// @brief Show a notification
uint32_t vg_notification_show(vg_notification_manager_t *mgr,
                              vg_notification_type_t type,
                              const char *title,
                              const char *message,
                              uint32_t duration_ms);

/// @brief Show notification with action button
uint32_t vg_notification_show_with_action(vg_notification_manager_t *mgr,
                                          vg_notification_type_t type,
                                          const char *title,
                                          const char *message,
                                          uint32_t duration_ms,
                                          const char *action_label,
                                          void (*action_callback)(uint32_t, void *),
                                          void *user_data);

/// @brief Dismiss a notification
void vg_notification_dismiss(vg_notification_manager_t *mgr, uint32_t id);

/// @brief Dismiss all notifications
void vg_notification_dismiss_all(vg_notification_manager_t *mgr);

/// @brief Set notification position
void vg_notification_manager_set_position(vg_notification_manager_t *mgr,
                                          vg_notification_position_t position);

/// @brief Set font
void vg_notification_manager_set_font(vg_notification_manager_t *mgr, vg_font_t *font, float size);

//==========================================================================
// Floating Panel — absolute-positioned overlay widget
//==========================================================================

/// @brief Floating panel widget.
/// @details A lightweight overlay that draws at an absolute screen position
///          regardless of the normal layout hierarchy. Children added via
///          vg_floatingpanel_add_child() are reparented under the panel so hit
///          testing, focus, and destruction follow the normal widget tree, but
///          the panel paints them during the overlay pass so they appear above
///          normal widget content.
typedef struct vg_floatingpanel {
    vg_widget_t base; ///< Base widget (connected to root as last child).

    float abs_x; ///< Absolute screen X position.
    float abs_y; ///< Absolute screen Y position.
    float abs_w; ///< Panel width in pixels.
    float abs_h; ///< Panel height in pixels.

    uint32_t bg_color;     ///< Background fill color (0xAARRGGBB).
    uint32_t border_color; ///< Border color (0xAARRGGBB).
    float border_width;    ///< Border width in pixels (0 = no border).

    bool dragging;       ///< True while the panel is being dragged.
    float drag_offset_x; ///< Pointer X offset from panel origin during drag.
    float drag_offset_y; ///< Pointer Y offset from panel origin during drag.
} vg_floatingpanel_t;

/// @brief Create a floating panel connected to @p root.
vg_floatingpanel_t *vg_floatingpanel_create(vg_widget_t *root);

/// @brief Destroy and free a floating panel.
void vg_floatingpanel_destroy(vg_floatingpanel_t *panel);

/// @brief Set the absolute screen position of the panel.
void vg_floatingpanel_set_position(vg_floatingpanel_t *panel, float x, float y);

/// @brief Set the panel dimensions.
void vg_floatingpanel_set_size(vg_floatingpanel_t *panel, float w, float h);

/// @brief Show or hide the floating panel.
void vg_floatingpanel_set_visible(vg_floatingpanel_t *panel, int visible);

/// @brief Add a widget as a child of the floating panel.
void vg_floatingpanel_add_child(vg_floatingpanel_t *panel, vg_widget_t *child);

#ifdef __cplusplus
}
#endif
