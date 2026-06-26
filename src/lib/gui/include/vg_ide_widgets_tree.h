//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/include/vg_ide_widgets_tree.h
// Purpose: TreeView, MenuBar, and ContextMenu widget declarations — hierarchical
//          navigation controls and popup/pull-down menu systems.
// Key invariants:
//   - All create functions return NULL on allocation failure.
//   - String parameters are copied internally unless documented otherwise.
//   - TreeView node depth is unlimited; rendering uses iterative traversal.
// Ownership/Lifetime:
//   - TreeView and MenuBar are owned by their parent in the widget tree.
//   - ContextMenu may be created without a parent and must be explicitly
//     destroyed.
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
// ContextMenu Widget
//=============================================================================

/// @brief ContextMenu widget structure
typedef struct vg_contextmenu {
    vg_widget_t base;

    // Menu items (reuses vg_menu_item_t structure)
    vg_menu_item_t **items;        ///< Array of items
    size_t item_count;             ///< Number of items
    size_t item_capacity;          ///< Allocated capacity
    vg_menu_item_t *retired_items; ///< Removed items kept until menu destroy for stale handles

    // Positioning
    int anchor_x; ///< Screen X where menu appears
    int anchor_y; ///< Screen Y where menu appears

    // State
    bool is_visible;                       ///< Is menu visible
    int hovered_index;                     ///< Hovered item index (-1 if none)
    int clicked_index;                     ///< Last clicked item index (-1 if none, edge-triggered)
    struct vg_contextmenu *active_submenu; ///< Open submenu
    struct vg_contextmenu *parent_menu;    ///< Parent menu (for submenus)
    struct vg_menu_item *parent_item;      ///< Parent item that owns this submenu, if any

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
    void *user_data;       ///< Legacy selection user data.
    void *on_select_data;  ///< User data for selection callback.
    void *on_dismiss_data; ///< User data for dismiss callback.
    void (*on_select)(struct vg_contextmenu *menu,
                      vg_menu_item_t *item,
                      void *user_data);                               ///< Selection callback
    void (*on_dismiss)(struct vg_contextmenu *menu, void *user_data); ///< Dismiss callback
} vg_contextmenu_t;

/// @brief Create a new context menu (not attached to a widget or parent).
/// @return New context menu or NULL on failure.
vg_contextmenu_t *vg_contextmenu_create(void);

/// @brief Destroy a context menu and free all items.
/// @param menu Context menu to destroy (may be NULL).
void vg_contextmenu_destroy(vg_contextmenu_t *menu);

/// @brief Add a clickable item to the context menu.
/// @param menu      Context menu.
/// @param label     Item label text (copied internally).
/// @param shortcut  Keyboard shortcut display string, e.g. "Ctrl+C" (copied; may be NULL).
/// @param action    Callback invoked when the item is selected (may be NULL).
/// @param user_data User data passed to @p action.
/// @return Newly added menu item, or NULL on failure.
vg_menu_item_t *vg_contextmenu_add_item(vg_contextmenu_t *menu,
                                        const char *label,
                                        const char *shortcut,
                                        void (*action)(void *),
                                        void *user_data);

/// @brief Add a nested submenu item to the context menu.
/// @param menu    Context menu.
/// @param label   Label for the submenu item (copied internally).
/// @param submenu Submenu shown when this item is hovered.
/// @return Menu item representing the submenu entry, or NULL on failure.
vg_menu_item_t *vg_contextmenu_add_submenu(vg_contextmenu_t *menu,
                                           const char *label,
                                           vg_contextmenu_t *submenu);

/// @brief Add a horizontal separator line to the context menu.
/// @param menu Context menu.
/// @return The separator menu item, or NULL on failure.
vg_menu_item_t *vg_contextmenu_add_separator(vg_contextmenu_t *menu);

/// @brief Remove and free all items from the context menu.
/// @param menu Context menu.
void vg_contextmenu_clear(vg_contextmenu_t *menu);

/// @brief Enable or disable a menu item (greyed out when disabled).
/// @param item    Menu item.
/// @param enabled true to make the item interactive.
void vg_contextmenu_item_set_enabled(vg_menu_item_t *item, bool enabled);

/// @brief Set the checked (tick mark) state of a menu item.
/// @param item    Menu item.
/// @param checked true to show a check mark beside the item.
void vg_contextmenu_item_set_checked(vg_menu_item_t *item, bool checked);

/// @brief Set the icon displayed beside a menu item.
/// @details Takes ownership of the icon payload (deep copy not made).
/// @param item Menu item.
/// @param icon Icon to display.
void vg_contextmenu_item_set_icon(vg_menu_item_t *item, vg_icon_t icon);

/// @brief Show the context menu at specific screen coordinates.
/// @param menu Context menu.
/// @param x    Horizontal position in screen pixels.
/// @param y    Vertical position in screen pixels.
void vg_contextmenu_show_at(vg_contextmenu_t *menu, int x, int y);

/// @brief Show the context menu offset from a widget's origin.
/// @param menu     Context menu.
/// @param widget   Reference widget.
/// @param offset_x Horizontal offset from the widget's left edge in pixels.
/// @param offset_y Vertical offset from the widget's top edge in pixels.
void vg_contextmenu_show_for_widget(vg_contextmenu_t *menu,
                                    vg_widget_t *widget,
                                    int offset_x,
                                    int offset_y);

/// @brief Hide the context menu.
/// @param menu Context menu.
void vg_contextmenu_dismiss(vg_contextmenu_t *menu);

/// @brief Set the callback fired when an item is selected.
/// @param menu      Context menu.
/// @param callback  Handler called with the menu, selected item, and user_data.
/// @param user_data User data passed to the handler.
void vg_contextmenu_set_on_select(vg_contextmenu_t *menu,
                                  void (*callback)(vg_contextmenu_t *, vg_menu_item_t *, void *),
                                  void *user_data);

/// @brief Set the callback fired when the menu is dismissed without a selection.
/// @param menu      Context menu.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_contextmenu_set_on_dismiss(vg_contextmenu_t *menu,
                                   void (*callback)(vg_contextmenu_t *, void *),
                                   void *user_data);

/// @brief Attach a context menu to a widget so it opens on right-click.
/// @param widget Widget that should trigger the menu.
/// @param menu   Context menu to show on right-click.
void vg_contextmenu_register_for_widget(vg_widget_t *widget, vg_contextmenu_t *menu);

/// @brief Detach any registered context menu from a widget.
/// @param widget Widget to detach the menu from.
void vg_contextmenu_unregister_for_widget(vg_widget_t *widget);

/// @brief Forward an event to a widget's registered context menu.
/// @param widget Widget whose context menu should receive the event.
/// @param event  Event to forward.
/// @return true if the context menu handled the event.
bool vg_contextmenu_process_event(vg_widget_t *widget, vg_event_t *event);

/// @brief Set the font used to render menu item labels.
/// @param menu Context menu.
/// @param font Font handle.
/// @param size Font size in pixels.
void vg_contextmenu_set_font(vg_contextmenu_t *menu, vg_font_t *font, float size);

/// @brief Apply theme colors to a context menu and all nested submenus.
/// @details Copies popup background, hover, text, disabled, border, and
///          separator colors from @p theme. Font fields are intentionally left
///          untouched so app-level font inheritance and theme refresh can be
///          applied independently.
/// @param menu Context menu root to update; may be NULL.
/// @param theme Theme to copy colors from; NULL resolves the current theme.
void vg_contextmenu_apply_theme(vg_contextmenu_t *menu, const vg_theme_t *theme);

//=============================================================================
// TreeView Widget
//=============================================================================

/// @brief Tree node structure
typedef struct vg_tree_node {
    uint64_t magic;              ///< Live-node sentinel for stale handle detection
    struct vg_treeview *owner;   ///< Owning tree while node is live
    char *text;                  ///< Node text (owned)
    size_t text_len;             ///< Node text length in bytes
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
    struct vg_tree_node *retired_next; ///< Retired-subtree list link
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

    vg_tree_node_t *root;                 ///< Root node (hidden, children are top-level)
    vg_tree_node_t *selected;             ///< Currently selected node
    vg_tree_node_t *prev_selected;        ///< Previous selection (for change detection)
    vg_tree_node_t *retired_nodes;        ///< Detached stale node subtrees freed on tree destroy
    uint64_t selection_revision;          ///< Incremented whenever logical selection changes
    uint64_t reported_selection_revision; ///< Last selection revision reported to runtime callers
    vg_font_t *font;                      ///< Font for rendering
    float font_size;                      ///< Font size

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

/// @brief Create a new tree view widget.
/// @param parent Parent widget (can be NULL).
/// @return New tree view or NULL on failure.
vg_treeview_t *vg_treeview_create(vg_widget_t *parent);

/// @brief Get the hidden root node (children of root are top-level items).
/// @param tree Tree view widget.
/// @return Root node pointer.
vg_tree_node_t *vg_treeview_get_root(vg_treeview_t *tree);

/// @brief Check whether a node handle still belongs to a live tree.
/// @param node Node handle to test.
/// @return true if the node is live; false if it has been removed or the tree destroyed.
bool vg_tree_node_is_live(const vg_tree_node_t *node);

/// @brief Add a new child node to a parent.
/// @param tree   Tree view widget.
/// @param parent Parent node (pass vg_treeview_get_root() for top-level items).
/// @param text   Node label text (copied internally).
/// @return Newly created node, or NULL on failure.
vg_tree_node_t *vg_treeview_add_node(vg_treeview_t *tree, vg_tree_node_t *parent, const char *text);

/// @brief Remove a node and all of its descendants from the tree.
/// @param tree Tree view widget.
/// @param node Node to remove (must belong to @p tree).
void vg_treeview_remove_node(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Remove all nodes from the tree, retiring stale handles until destroy or prune.
/// @param tree Tree view widget.
void vg_treeview_clear(vg_treeview_t *tree);

/// @brief Free retired node tombstones after all stale node handles are discarded.
/// @param tree Tree view widget.
void vg_treeview_prune_retired_nodes(vg_treeview_t *tree);

/// @brief Expand a node, showing its children.
/// @param tree Tree view widget.
/// @param node Node to expand.
void vg_treeview_expand(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Collapse a node, hiding its children.
/// @param tree Tree view widget.
/// @param node Node to collapse.
void vg_treeview_collapse(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Toggle a node between expanded and collapsed.
/// @param tree Tree view widget.
/// @param node Node to toggle.
void vg_treeview_toggle(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Select a node, deselecting the previous selection.
/// @param tree Tree view widget.
/// @param node Node to select, or NULL to deselect all.
void vg_treeview_select(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Return the visible node under a window-space point.
/// @param tree Tree view widget.
/// @param x Window-space X coordinate.
/// @param y Window-space Y coordinate.
/// @return Node under the point, or NULL if the point is outside rows.
vg_tree_node_t *vg_treeview_node_at(vg_treeview_t *tree, float x, float y);

/// @brief Scroll the view so that a node is visible.
/// @param tree Tree view widget.
/// @param node Node to scroll into view.
void vg_treeview_scroll_to(vg_treeview_t *tree, vg_tree_node_t *node);

/// @brief Set arbitrary caller data on a node.
/// @param node Node to modify.
/// @param data Caller-owned pointer stored without copying.
void vg_tree_node_set_data(vg_tree_node_t *node, void *data);

/// @brief Set the font used for node text rendering.
/// @param tree Tree view widget.
/// @param font Font handle.
/// @param size Font size in pixels.
void vg_treeview_set_font(vg_treeview_t *tree, vg_font_t *font, float size);

/// @brief Set the callback fired when the selected node changes.
/// @param tree      Tree view widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_treeview_set_on_select(vg_treeview_t *tree,
                               vg_tree_select_callback_t callback,
                               void *user_data);

/// @brief Set the callback fired when a node is expanded or collapsed.
/// @param tree      Tree view widget.
/// @param callback  Handler called with the node and new expanded state.
/// @param user_data User data passed to the handler.
void vg_treeview_set_on_expand(vg_treeview_t *tree,
                               vg_tree_expand_callback_t callback,
                               void *user_data);

/// @brief Set the callback fired when a node is double-clicked (activated).
/// @param tree      Tree view widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_treeview_set_on_activate(vg_treeview_t *tree,
                                 vg_tree_activate_callback_t callback,
                                 void *user_data);

// --- Icon Support ---

/// @brief Set the icon displayed beside a node.
/// @param node Node to modify.
/// @param icon Icon specification (tree takes ownership).
void vg_tree_node_set_icon(vg_tree_node_t *node, vg_icon_t icon);

/// @brief Set the alternate icon shown when a node is in the expanded state.
/// @param node Icon shown while the node is expanded (e.g. open-folder glyph).
void vg_tree_node_set_expanded_icon(vg_tree_node_t *node, vg_icon_t icon);

// --- Drag and Drop ---

/// @brief Enable or disable drag-and-drop reordering.
/// @param tree    Tree view widget.
/// @param enabled true to allow nodes to be dragged.
void vg_treeview_set_drag_enabled(vg_treeview_t *tree, bool enabled);

/// @brief Set all three drag-and-drop callbacks at once.
/// @param tree      Tree view widget.
/// @param can_drag  Returns true if @p node may be dragged.
/// @param can_drop  Returns true if @p source may be dropped onto @p target at @p position.
/// @param on_drop   Called to perform the actual reparenting/reordering.
/// @param user_data User data passed to all three callbacks.
void vg_treeview_set_drag_callbacks(vg_treeview_t *tree,
                                    vg_tree_can_drag_callback_t can_drag,
                                    vg_tree_can_drop_callback_t can_drop,
                                    vg_tree_on_drop_callback_t on_drop,
                                    void *user_data);

// --- Lazy Loading ---

/// @brief Set the callback invoked when a node with @c has_children is first expanded.
/// @param tree      Tree view widget.
/// @param callback  Handler that should call vg_treeview_add_node to populate children.
/// @param user_data User data passed to the handler.
void vg_treeview_set_on_load_children(vg_treeview_t *tree,
                                      vg_tree_load_children_callback_t callback,
                                      void *user_data);

/// @brief Mark a node as having children without actually creating them yet (lazy loading).
/// @param node         Node to modify.
/// @param has_children true to show the expand arrow even when the child list is empty.
void vg_tree_node_set_has_children(vg_tree_node_t *node, bool has_children);

/// @brief Set or clear the "loading" spinner shown while children are being fetched.
/// @param node    Node to modify.
/// @param loading true to display a loading indicator.
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
    uint64_t magic;                           ///< Live-item sentinel for stale handle detection
    char *text;                               ///< Item text (owned, heap-allocated)
    char *shortcut;                           ///< Keyboard shortcut text (owned, heap-allocated)
    vg_accelerator_t accel;                   ///< Parsed accelerator
    void (*action)(void *data);               ///< Action callback
    void *action_data;                        ///< Action data
    bool enabled;                             ///< Is item enabled
    bool checked;                             ///< Is item checked (for toggles)
    bool checkable;                           ///< Can item display/toggle checked state
    bool separator;                           ///< Is this a separator
    bool was_clicked;                         ///< Set true when item is clicked (cleared on read)
    vg_icon_t icon;                           ///< Optional icon (VG_ICON_NONE = no icon)
    struct vg_menu *parent_menu;              ///< Owning menu for change propagation.
    struct vg_contextmenu *owner_contextmenu; ///< Owning context menu for change propagation.
    struct vg_menu *submenu;                  ///< Submenu (if any)
    struct vg_menu_item *next;
    struct vg_menu_item *prev;
    struct vg_menu_item *retired_next; ///< Retired-item list link
};

/// @brief Menu structure (forward declared in vg_ide_widgets_common.h)
struct vg_menu {
    uint64_t magic;                   ///< Live-menu sentinel for stale handle detection
    char *title;                      ///< Menu title (owned, heap-allocated)
    struct vg_menubar *owner_menubar; ///< Owning menubar for change propagation.
    vg_menu_item_t *first_item;
    vg_menu_item_t *last_item;
    vg_menu_item_t *retired_items; ///< Removed items kept until menu destroy for stale handles
    int item_count;
    struct vg_menu *next;
    struct vg_menu *prev;
    struct vg_menu *retired_next; ///< Retired-menu list link
    bool open;                    ///< Is menu currently open
    bool enabled;                 ///< Is menu enabled (default true)
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
    vg_menu_t *retired_menus;    ///< Removed menus kept until menubar destroy for stale handles
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

/// @brief Create a new menu bar widget.
/// @param parent Parent widget (can be NULL).
/// @return New menu bar or NULL on failure.
vg_menubar_t *vg_menubar_create(vg_widget_t *parent);

/// @brief Add a top-level pull-down menu to the menu bar.
/// @param menubar Menu bar widget.
/// @param title   Menu title displayed in the bar (copied internally).
/// @return Newly added menu, or NULL on failure.
vg_menu_t *vg_menubar_add_menu(vg_menubar_t *menubar, const char *title);

/// @brief Add a clickable item to a menu.
/// @param menu     Menu to add the item to.
/// @param text     Item label text (copied internally).
/// @param shortcut Keyboard shortcut display string (copied; may be NULL).
/// @param action   Callback invoked when the item is selected (may be NULL).
/// @param data     User data passed to @p action.
/// @return Newly added menu item, or NULL on failure.
vg_menu_item_t *vg_menu_add_item(
    vg_menu_t *menu, const char *text, const char *shortcut, void (*action)(void *), void *data);

/// @brief Add a horizontal separator to a menu.
/// @param menu Menu to add the separator to.
/// @return Separator menu item, or NULL on failure.
vg_menu_item_t *vg_menu_add_separator(vg_menu_t *menu);

/// @brief Add a nested submenu to an existing menu.
/// @param menu  Parent menu.
/// @param title Submenu title (copied internally).
/// @return New submenu, or NULL on failure.
vg_menu_t *vg_menu_add_submenu(vg_menu_t *menu, const char *title);

/// @brief Enable or disable a menu item.
/// @param item    Menu item.
/// @param enabled true to make the item interactive; false to grey it out.
void vg_menu_item_set_enabled(vg_menu_item_t *item, bool enabled);

/// @brief Set the checked (tick mark) state of a menu item.
/// @param item    Menu item.
/// @param checked true to show a check mark beside the label.
void vg_menu_item_set_checked(vg_menu_item_t *item, bool checked);

/// @brief Remove and free a specific item from a menu.
/// @param menu Menu that owns the item.
/// @param item Item to remove.
void vg_menu_remove_item(vg_menu_t *menu, vg_menu_item_t *item);

/// @brief Remove and free all items from a menu.
/// @param menu Menu to clear.
void vg_menu_clear(vg_menu_t *menu);

/// @brief Remove a top-level menu from the menu bar and free it.
/// @param menubar Menu bar widget.
/// @param menu    Menu to remove.
void vg_menubar_remove_menu(vg_menubar_t *menubar, vg_menu_t *menu);

/// @brief Set the font used to render menu bar and item labels.
/// @param menubar Menu bar widget.
/// @param font    Font handle.
/// @param size    Font size in pixels.
void vg_menubar_set_font(vg_menubar_t *menubar, vg_font_t *font, float size);

// --- Keyboard Accelerators ---

/// @brief Parse an accelerator string into a key + modifier combination.
/// @param shortcut Accelerator string, e.g. "Ctrl+S" or "Cmd+Shift+N".
/// @param accel    Output structure that receives the parsed key and modifiers.
/// @return true if parsing succeeded; false for an unrecognised format.
bool vg_parse_accelerator(const char *shortcut, vg_accelerator_t *accel);

/// @brief Register a keyboard shortcut that triggers a menu item.
/// @param menubar  Menu bar that owns the accelerator table.
/// @param item     Menu item to trigger.
/// @param shortcut Accelerator string (see vg_parse_accelerator).
void vg_menubar_register_accelerator(vg_menubar_t *menubar,
                                     vg_menu_item_t *item,
                                     const char *shortcut);

/// @brief Rebuild the accelerator lookup table from all items in all menus.
/// @param menubar Menu bar widget.
void vg_menubar_rebuild_accelerators(vg_menubar_t *menubar);

/// @brief Handle a key event, firing the matching accelerator if found.
/// @param menubar   Menu bar widget.
/// @param key       Virtual key code (VG_KEY_*).
/// @param modifiers Active modifier flags (VG_MOD_*).
/// @return true if an accelerator matched and its action was called.
bool vg_menubar_handle_accelerator(vg_menubar_t *menubar, int key, uint32_t modifiers);

/// @brief Return true if a menu/context-menu item handle is currently live.
/// @param item Item handle to test.
/// @return true when item belongs to a live menu or context menu.
bool vg_menu_item_is_live(const vg_menu_item_t *item);
/// @brief Returns true when @p menu is still part of a live menubar menu tree.
bool vg_menu_is_live(const vg_menu_t *menu);
/// @brief Returns true when @p menu is still a live context menu.
bool vg_contextmenu_is_live(const vg_contextmenu_t *menu);

#ifdef __cplusplus
}
#endif
