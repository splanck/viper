//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/include/vg_ide_widgets_tree.h
// Purpose: TreeView, MenuBar, and ContextMenu widget declarations.
// Key invariants:
//   - All create functions return NULL on allocation failure.
//   - String parameters are copied internally unless documented otherwise.
// Ownership/Lifetime:
//   - TreeView and MenuBar are owned by their parent in the widget tree.
//   - ContextMenu may be created without a parent and must be explicitly
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
// ContextMenu Widget
//=============================================================================

/// @brief ContextMenu widget structure
typedef struct vg_contextmenu {
    vg_widget_t base;

    // Menu items (reuses vg_menu_item_t structure)
    vg_menu_item_t **items; ///< Array of items
    size_t item_count;      ///< Number of items
    size_t item_capacity;   ///< Allocated capacity

    // Positioning
    int anchor_x; ///< Screen X where menu appears
    int anchor_y; ///< Screen Y where menu appears

    // State
    bool is_visible;                       ///< Is menu visible
    int hovered_index;                     ///< Hovered item index (-1 if none)
    int clicked_index;                     ///< Last clicked item index (-1 if none, edge-triggered)
    struct vg_contextmenu *active_submenu; ///< Open submenu
    struct vg_contextmenu *parent_menu;    ///< Parent menu (for submenus)

    // Styling
    uint32_t min_width;  ///< Minimum menu width (default: 150)
    uint32_t max_height; ///< Maximum height before scrolling

    // Font
    vg_font_t *font; ///< Font for text
    float font_size; ///< Font size

    // Colors
    uint32_t bg_color;        ///< Background color
    uint32_t hover_color;     ///< Hover color
    uint32_t text_color;      ///< Text color
    uint32_t disabled_color;  ///< Disabled text color
    uint32_t border_color;    ///< Border color
    uint32_t separator_color; ///< Separator color

    // Callbacks
    void *user_data; ///< User data
    void (*on_select)(struct vg_contextmenu *menu,
                      vg_menu_item_t *item,
                      void *user_data);                               ///< Selection callback
    void (*on_dismiss)(struct vg_contextmenu *menu, void *user_data); ///< Dismiss callback
} vg_contextmenu_t;

/// @brief Create a new context menu widget
vg_contextmenu_t *vg_contextmenu_create(void);

/// @brief Destroy a context menu widget
void vg_contextmenu_destroy(vg_contextmenu_t *menu);

/// @brief Add an item to the context menu
vg_menu_item_t *vg_contextmenu_add_item(vg_contextmenu_t *menu,
                                        const char *label,
                                        const char *shortcut,
                                        void (*action)(void *),
                                        void *user_data);

/// @brief Add a submenu to the context menu
vg_menu_item_t *vg_contextmenu_add_submenu(vg_contextmenu_t *menu,
                                           const char *label,
                                           vg_contextmenu_t *submenu);

/// @brief Add a separator to the context menu
void vg_contextmenu_add_separator(vg_contextmenu_t *menu);

/// @brief Clear all items from the context menu
void vg_contextmenu_clear(vg_contextmenu_t *menu);

/// @brief Set item enabled state
void vg_contextmenu_item_set_enabled(vg_menu_item_t *item, bool enabled);

/// @brief Set item checked state
void vg_contextmenu_item_set_checked(vg_menu_item_t *item, bool checked);

/// @brief Set item icon
void vg_contextmenu_item_set_icon(vg_menu_item_t *item, vg_icon_t icon);

/// @brief Show context menu at position
void vg_contextmenu_show_at(vg_contextmenu_t *menu, int x, int y);

/// @brief Show context menu relative to a widget
void vg_contextmenu_show_for_widget(vg_contextmenu_t *menu,
                                    vg_widget_t *widget,
                                    int offset_x,
                                    int offset_y);

/// @brief Dismiss (hide) the context menu
void vg_contextmenu_dismiss(vg_contextmenu_t *menu);

/// @brief Set selection callback
void vg_contextmenu_set_on_select(vg_contextmenu_t *menu,
                                  void (*callback)(vg_contextmenu_t *, vg_menu_item_t *, void *),
                                  void *user_data);

/// @brief Set dismiss callback
void vg_contextmenu_set_on_dismiss(vg_contextmenu_t *menu,
                                   void (*callback)(vg_contextmenu_t *, void *),
                                   void *user_data);

/// @brief Register a context menu for a widget (shown on right-click)
void vg_contextmenu_register_for_widget(vg_widget_t *widget, vg_contextmenu_t *menu);

/// @brief Unregister context menu from a widget
void vg_contextmenu_unregister_for_widget(vg_widget_t *widget);

/// @brief Process an event for a registered widget (call from event dispatch loop)
bool vg_contextmenu_process_event(vg_widget_t *widget, vg_event_t *event);

/// @brief Set font for context menu
void vg_contextmenu_set_font(vg_contextmenu_t *menu, vg_font_t *font, float size);

//=============================================================================
// TreeView Widget
//=============================================================================

/// @brief Tree node structure
typedef struct vg_tree_node {
    const char *text;            ///< Node text (owned)
    void *user_data;             ///< User data associated with node
    bool owns_user_data;         ///< True when user_data is owned and should be freed
    bool expanded;               ///< Is node expanded
    bool selected;               ///< Is node selected
    bool has_children;           ///< Does node have children (for lazy loading)
    bool loading;                ///< Is node loading children (lazy loading)
    struct vg_tree_node *parent; ///< Parent node
    struct vg_tree_node *first_child;
    struct vg_tree_node *last_child;
    struct vg_tree_node *next_sibling;
    struct vg_tree_node *prev_sibling;
    int child_count;
    int depth; ///< Depth in tree (0 = root)

    // Icon support
    vg_icon_t icon;          ///< Node icon
    vg_icon_t expanded_icon; ///< Icon when expanded (optional, for folders)
} vg_tree_node_t;

/// @brief TreeView callback types
typedef void (*vg_tree_select_callback_t)(vg_widget_t *tree, vg_tree_node_t *node, void *user_data);
typedef void (*vg_tree_expand_callback_t)(vg_widget_t *tree,
                                          vg_tree_node_t *node,
                                          bool expanded,
                                          void *user_data);
typedef void (*vg_tree_activate_callback_t)(vg_widget_t *tree,
                                            vg_tree_node_t *node,
                                            void *user_data);

/// @brief Drop position for drag-and-drop
typedef enum vg_tree_drop_position {
    VG_TREE_DROP_BEFORE, ///< Drop before target node
    VG_TREE_DROP_AFTER,  ///< Drop after target node
    VG_TREE_DROP_INTO    ///< Drop as child of target node
} vg_tree_drop_position_t;

/// @brief Drag-and-drop callback types
typedef bool (*vg_tree_can_drag_callback_t)(vg_tree_node_t *node, void *user_data);
typedef bool (*vg_tree_can_drop_callback_t)(vg_tree_node_t *source,
                                            vg_tree_node_t *target,
                                            vg_tree_drop_position_t position,
                                            void *user_data);
typedef void (*vg_tree_on_drop_callback_t)(vg_tree_node_t *source,
                                           vg_tree_node_t *target,
                                           vg_tree_drop_position_t position,
                                           void *user_data);

/// @brief Lazy loading callback type
typedef void (*vg_tree_load_children_callback_t)(struct vg_treeview *tree,
                                                 vg_tree_node_t *node,
                                                 void *user_data);

/// @brief TreeView widget structure
typedef struct vg_treeview {
    vg_widget_t base;

    vg_tree_node_t *root;          ///< Root node (hidden, children are top-level)
    vg_tree_node_t *selected;      ///< Currently selected node
    vg_tree_node_t *prev_selected; ///< Previous selection (for change detection)
    vg_font_t *font;               ///< Font for rendering
    float font_size;               ///< Font size

    // Appearance
    float row_height;     ///< Height of each row
    float indent_size;    ///< Indentation per level
    float icon_size;      ///< Icon size
    float icon_gap;       ///< Gap between icon and text
    uint32_t text_color;  ///< Text color
    uint32_t selected_bg; ///< Selected item background
    uint32_t hover_bg;    ///< Hover background

    // Scrolling
    float scroll_y;    ///< Vertical scroll position
    int visible_start; ///< First visible row index
    int visible_count; ///< Number of visible rows

    // Callbacks
    vg_tree_select_callback_t on_select;
    void *on_select_data;
    vg_tree_expand_callback_t on_expand;
    void *on_expand_data;
    vg_tree_activate_callback_t on_activate;
    void *on_activate_data;

    // Lazy loading
    vg_tree_load_children_callback_t on_load_children;
    void *on_load_children_data;

    // Drag and drop
    bool drag_enabled;                     ///< Enable drag-and-drop
    vg_tree_node_t *drag_node;             ///< Node being dragged
    int drag_start_x;                      ///< Drag start X position
    int drag_start_y;                      ///< Drag start Y position
    bool is_dragging;                      ///< Currently dragging
    vg_tree_node_t *drop_target;           ///< Current drop target
    vg_tree_drop_position_t drop_position; ///< Current drop position

    // Drag callbacks
    vg_tree_can_drag_callback_t can_drag;
    vg_tree_can_drop_callback_t can_drop;
    vg_tree_on_drop_callback_t on_drop;
    void *drag_user_data;

    // State
    vg_tree_node_t *hovered; ///< Currently hovered node
    bool suppress_click;     ///< Swallow the synthetic click that follows a drag
} vg_treeview_t;

/// @brief Create a new tree view widget
vg_treeview_t *vg_treeview_create(vg_widget_t *parent);

/// @brief Get tree root node
vg_tree_node_t *vg_treeview_get_root(vg_treeview_t *tree);

/// @brief Add a child node
vg_tree_node_t *vg_treeview_add_node(vg_treeview_t *tree, vg_tree_node_t *parent, const char *text);

/// @brief Remove a node and all its children
void vg_treeview_remove_node(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Clear all nodes
void vg_treeview_clear(vg_treeview_t *tree);

/// @brief Expand a node
void vg_treeview_expand(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Collapse a node
void vg_treeview_collapse(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Toggle node expansion
void vg_treeview_toggle(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Select a node
void vg_treeview_select(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Scroll to make a node visible
void vg_treeview_scroll_to(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Set node user data
void vg_tree_node_set_data(vg_tree_node_t *node, void *data);

/// @brief Set font for tree view
void vg_treeview_set_font(vg_treeview_t *tree, vg_font_t *font, float size);

/// @brief Set selection callback
void vg_treeview_set_on_select(vg_treeview_t *tree,
                               vg_tree_select_callback_t callback,
                               void *user_data);

/// @brief Set expand callback
void vg_treeview_set_on_expand(vg_treeview_t *tree,
                               vg_tree_expand_callback_t callback,
                               void *user_data);

/// @brief Set activate (double-click) callback
void vg_treeview_set_on_activate(vg_treeview_t *tree,
                                 vg_tree_activate_callback_t callback,
                                 void *user_data);

// --- Icon Support ---

/// @brief Set node icon
void vg_tree_node_set_icon(vg_tree_node_t *node, vg_icon_t icon);

/// @brief Set node expanded icon (used when node is expanded, e.g., open folder)
void vg_tree_node_set_expanded_icon(vg_tree_node_t *node, vg_icon_t icon);

// --- Drag and Drop ---

/// @brief Enable or disable drag-and-drop for tree view
void vg_treeview_set_drag_enabled(vg_treeview_t *tree, bool enabled);

/// @brief Set drag-and-drop callbacks
void vg_treeview_set_drag_callbacks(vg_treeview_t *tree,
                                    vg_tree_can_drag_callback_t can_drag,
                                    vg_tree_can_drop_callback_t can_drop,
                                    vg_tree_on_drop_callback_t on_drop,
                                    void *user_data);

// --- Lazy Loading ---

/// @brief Set lazy loading callback
void vg_treeview_set_on_load_children(vg_treeview_t *tree,
                                      vg_tree_load_children_callback_t callback,
                                      void *user_data);

/// @brief Set whether a node has children (for lazy loading indicator)
void vg_tree_node_set_has_children(vg_tree_node_t *node, bool has_children);

/// @brief Set node loading state (shows spinner while loading children)
void vg_tree_node_set_loading(vg_tree_node_t *node, bool loading);

//=============================================================================
// MenuBar Widget
//=============================================================================

/// @brief Parsed keyboard accelerator
typedef struct vg_accelerator {
    int key;            ///< Key code (VG_KEY_*)
    uint32_t modifiers; ///< Modifier flags (VG_MOD_*)
} vg_accelerator_t;

/// @brief Menu item structure (forward declared in vg_ide_widgets_common.h)
struct vg_menu_item {
    char *text;                  ///< Item text (owned, heap-allocated)
    char *shortcut;              ///< Keyboard shortcut text (owned, heap-allocated)
    vg_accelerator_t accel;      ///< Parsed accelerator
    void (*action)(void *data);  ///< Action callback
    void *action_data;           ///< Action data
    bool enabled;                ///< Is item enabled
    bool checked;                ///< Is item checked (for toggles)
    bool separator;              ///< Is this a separator
    bool was_clicked;            ///< Set true when item is clicked (cleared on read)
    vg_icon_t icon;              ///< Optional icon (VG_ICON_NONE = no icon)
    struct vg_menu *parent_menu; ///< Owning menu for change propagation.
    struct vg_menu *submenu;     ///< Submenu (if any)
    struct vg_menu_item *next;
    struct vg_menu_item *prev;
};

/// @brief Menu structure (forward declared in vg_ide_widgets_common.h)
struct vg_menu {
    char *title;                      ///< Menu title (owned, heap-allocated)
    struct vg_menubar *owner_menubar; ///< Owning menubar for change propagation.
    vg_menu_item_t *first_item;
    vg_menu_item_t *last_item;
    int item_count;
    struct vg_menu *next;
    struct vg_menu *prev;
    bool open;    ///< Is menu currently open
    bool enabled; ///< Is menu enabled (default true)
};

/// @brief Accelerator table entry
typedef struct vg_accel_entry {
    vg_accelerator_t accel; ///< Accelerator key
    vg_menu_item_t *item;   ///< Menu item to trigger
    struct vg_accel_entry *next;
} vg_accel_entry_t;

/// @brief MenuBar widget structure
typedef struct vg_menubar {
    vg_widget_t base;

    vg_menu_t *first_menu;       ///< First menu
    vg_menu_t *last_menu;        ///< Last menu
    int menu_count;              ///< Number of menus
    vg_menu_t *open_menu;        ///< Currently open menu
    vg_menu_item_t *highlighted; ///< Currently highlighted item

    vg_font_t *font; ///< Font for rendering
    float font_size; ///< Font size

    // Appearance
    float height;            ///< Menu bar height
    float menu_padding;      ///< Horizontal padding for menu titles
    float item_padding;      ///< Padding for menu items
    uint32_t bg_color;       ///< Background color
    uint32_t text_color;     ///< Text color
    uint32_t highlight_bg;   ///< Highlighted item background
    uint32_t disabled_color; ///< Disabled item text color

    // Keyboard accelerators
    vg_accel_entry_t *accel_table; ///< Accelerator lookup table

    // State
    bool menu_active;      ///< Is any menu active
    bool native_main_menu; ///< Mirrors to the native macOS app menubar.
} vg_menubar_t;

/// @brief Create a new menu bar widget
vg_menubar_t *vg_menubar_create(vg_widget_t *parent);

/// @brief Add a menu to the menu bar
vg_menu_t *vg_menubar_add_menu(vg_menubar_t *menubar, const char *title);

/// @brief Add an item to a menu
vg_menu_item_t *vg_menu_add_item(
    vg_menu_t *menu, const char *text, const char *shortcut, void (*action)(void *), void *data);

/// @brief Add a separator to a menu
vg_menu_item_t *vg_menu_add_separator(vg_menu_t *menu);

/// @brief Add a submenu
vg_menu_t *vg_menu_add_submenu(vg_menu_t *menu, const char *title);

/// @brief Set menu item enabled state
void vg_menu_item_set_enabled(vg_menu_item_t *item, bool enabled);

/// @brief Set menu item checked state
void vg_menu_item_set_checked(vg_menu_item_t *item, bool checked);

/// @brief Remove and free a specific item from a menu.
void vg_menu_remove_item(vg_menu_t *menu, vg_menu_item_t *item);

/// @brief Remove and free all items from a menu.
void vg_menu_clear(vg_menu_t *menu);

/// @brief Remove a menu from the menubar and free it.
void vg_menubar_remove_menu(vg_menubar_t *menubar, vg_menu_t *menu);

/// @brief Set font for menu bar
void vg_menubar_set_font(vg_menubar_t *menubar, vg_font_t *font, float size);

// --- Keyboard Accelerators ---

/// @brief Parse accelerator string (e.g., "Ctrl+S", "Cmd+Shift+N")
bool vg_parse_accelerator(const char *shortcut, vg_accelerator_t *accel);

/// @brief Register a keyboard accelerator
void vg_menubar_register_accelerator(vg_menubar_t *menubar,
                                     vg_menu_item_t *item,
                                     const char *shortcut);

/// @brief Rebuild accelerator table from all menu items
void vg_menubar_rebuild_accelerators(vg_menubar_t *menubar);

/// @brief Handle a key event, triggering accelerator if matched
bool vg_menubar_handle_accelerator(vg_menubar_t *menubar, int key, uint32_t modifiers);

#ifdef __cplusplus
}
#endif
