// vg_ide_widgets.h - IDE-specific widget library
#ifndef VG_IDE_WIDGETS_H
#define VG_IDE_WIDGETS_H

#include "vg_widget.h"
#include "vg_layout.h"
#include "vg_font.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// TreeView Widget
//=============================================================================

/// @brief Tree node structure
typedef struct vg_tree_node {
    const char* text;            ///< Node text (owned)
    void* user_data;             ///< User data associated with node
    bool expanded;               ///< Is node expanded
    bool selected;               ///< Is node selected
    bool has_children;           ///< Does node have children (for lazy loading)
    struct vg_tree_node* parent; ///< Parent node
    struct vg_tree_node* first_child;
    struct vg_tree_node* last_child;
    struct vg_tree_node* next_sibling;
    struct vg_tree_node* prev_sibling;
    int child_count;
    int depth;                   ///< Depth in tree (0 = root)
} vg_tree_node_t;

/// @brief TreeView callback types
typedef void (*vg_tree_select_callback_t)(vg_widget_t* tree, vg_tree_node_t* node, void* user_data);
typedef void (*vg_tree_expand_callback_t)(vg_widget_t* tree, vg_tree_node_t* node, bool expanded, void* user_data);
typedef void (*vg_tree_activate_callback_t)(vg_widget_t* tree, vg_tree_node_t* node, void* user_data);

/// @brief TreeView widget structure
typedef struct vg_treeview {
    vg_widget_t base;

    vg_tree_node_t* root;        ///< Root node (hidden, children are top-level)
    vg_tree_node_t* selected;    ///< Currently selected node
    vg_font_t* font;             ///< Font for rendering
    float font_size;             ///< Font size

    // Appearance
    float row_height;            ///< Height of each row
    float indent_size;           ///< Indentation per level
    float icon_size;             ///< Icon size
    float icon_gap;              ///< Gap between icon and text
    uint32_t text_color;         ///< Text color
    uint32_t selected_bg;        ///< Selected item background
    uint32_t hover_bg;           ///< Hover background

    // Scrolling
    float scroll_y;              ///< Vertical scroll position
    int visible_start;           ///< First visible row index
    int visible_count;           ///< Number of visible rows

    // Callbacks
    vg_tree_select_callback_t on_select;
    void* on_select_data;
    vg_tree_expand_callback_t on_expand;
    void* on_expand_data;
    vg_tree_activate_callback_t on_activate;
    void* on_activate_data;

    // State
    vg_tree_node_t* hovered;     ///< Currently hovered node
} vg_treeview_t;

/// @brief Create a new tree view widget
/// @param parent Parent widget (can be NULL)
/// @return New tree view widget or NULL on failure
vg_treeview_t* vg_treeview_create(vg_widget_t* parent);

/// @brief Get tree root node
/// @param tree TreeView widget
/// @return Root node
vg_tree_node_t* vg_treeview_get_root(vg_treeview_t* tree);

/// @brief Add a child node
/// @param tree TreeView widget
/// @param parent Parent node (NULL for root children)
/// @param text Node text
/// @return New node or NULL on failure
vg_tree_node_t* vg_treeview_add_node(vg_treeview_t* tree, vg_tree_node_t* parent, const char* text);

/// @brief Remove a node and all its children
/// @param tree TreeView widget
/// @param node Node to remove
void vg_treeview_remove_node(vg_treeview_t* tree, vg_tree_node_t* node);

/// @brief Clear all nodes
/// @param tree TreeView widget
void vg_treeview_clear(vg_treeview_t* tree);

/// @brief Expand a node
/// @param tree TreeView widget
/// @param node Node to expand
void vg_treeview_expand(vg_treeview_t* tree, vg_tree_node_t* node);

/// @brief Collapse a node
/// @param tree TreeView widget
/// @param node Node to collapse
void vg_treeview_collapse(vg_treeview_t* tree, vg_tree_node_t* node);

/// @brief Toggle node expansion
/// @param tree TreeView widget
/// @param node Node to toggle
void vg_treeview_toggle(vg_treeview_t* tree, vg_tree_node_t* node);

/// @brief Select a node
/// @param tree TreeView widget
/// @param node Node to select
void vg_treeview_select(vg_treeview_t* tree, vg_tree_node_t* node);

/// @brief Scroll to make a node visible
/// @param tree TreeView widget
/// @param node Node to scroll to
void vg_treeview_scroll_to(vg_treeview_t* tree, vg_tree_node_t* node);

/// @brief Set node user data
/// @param node Tree node
/// @param data User data
void vg_tree_node_set_data(vg_tree_node_t* node, void* data);

/// @brief Set font for tree view
/// @param tree TreeView widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_treeview_set_font(vg_treeview_t* tree, vg_font_t* font, float size);

/// @brief Set selection callback
/// @param tree TreeView widget
/// @param callback Selection handler
/// @param user_data User data
void vg_treeview_set_on_select(vg_treeview_t* tree, vg_tree_select_callback_t callback, void* user_data);

/// @brief Set expand callback
/// @param tree TreeView widget
/// @param callback Expand handler
/// @param user_data User data
void vg_treeview_set_on_expand(vg_treeview_t* tree, vg_tree_expand_callback_t callback, void* user_data);

/// @brief Set activate (double-click) callback
/// @param tree TreeView widget
/// @param callback Activate handler
/// @param user_data User data
void vg_treeview_set_on_activate(vg_treeview_t* tree, vg_tree_activate_callback_t callback, void* user_data);

//=============================================================================
// TabBar Widget
//=============================================================================

/// @brief Tab structure
typedef struct vg_tab {
    const char* title;           ///< Tab title (owned)
    const char* tooltip;         ///< Tab tooltip (owned)
    void* user_data;             ///< User data
    bool closable;               ///< Can tab be closed
    bool modified;               ///< Show modified indicator
    struct vg_tab* next;
    struct vg_tab* prev;
} vg_tab_t;

/// @brief Tab callbacks
typedef void (*vg_tab_select_callback_t)(vg_widget_t* tabbar, vg_tab_t* tab, void* user_data);
typedef bool (*vg_tab_close_callback_t)(vg_widget_t* tabbar, vg_tab_t* tab, void* user_data);
typedef void (*vg_tab_reorder_callback_t)(vg_widget_t* tabbar, vg_tab_t* tab, int new_index, void* user_data);

/// @brief TabBar widget structure
typedef struct vg_tabbar {
    vg_widget_t base;

    vg_tab_t* first_tab;         ///< First tab
    vg_tab_t* last_tab;          ///< Last tab
    vg_tab_t* active_tab;        ///< Currently active tab
    int tab_count;               ///< Number of tabs

    vg_font_t* font;             ///< Font for rendering
    float font_size;             ///< Font size

    // Appearance
    float tab_height;            ///< Tab height
    float tab_padding;           ///< Tab horizontal padding
    float close_button_size;     ///< Close button size
    float max_tab_width;         ///< Maximum tab width
    uint32_t active_bg;          ///< Active tab background
    uint32_t inactive_bg;        ///< Inactive tab background
    uint32_t text_color;         ///< Text color
    uint32_t close_color;        ///< Close button color

    // Scrolling (for many tabs)
    float scroll_x;              ///< Horizontal scroll offset
    float total_width;           ///< Total width of all tabs

    // Callbacks
    vg_tab_select_callback_t on_select;
    void* on_select_data;
    vg_tab_close_callback_t on_close;
    void* on_close_data;
    vg_tab_reorder_callback_t on_reorder;
    void* on_reorder_data;

    // State
    vg_tab_t* hovered_tab;       ///< Currently hovered tab
    bool close_button_hovered;   ///< Is close button hovered
    bool dragging;               ///< Is dragging a tab
    vg_tab_t* drag_tab;          ///< Tab being dragged
    float drag_x;                ///< Drag position
} vg_tabbar_t;

/// @brief Create a new tab bar widget
/// @param parent Parent widget (can be NULL)
/// @return New tab bar widget or NULL on failure
vg_tabbar_t* vg_tabbar_create(vg_widget_t* parent);

/// @brief Add a tab
/// @param tabbar TabBar widget
/// @param title Tab title
/// @param closable Can tab be closed
/// @return New tab or NULL on failure
vg_tab_t* vg_tabbar_add_tab(vg_tabbar_t* tabbar, const char* title, bool closable);

/// @brief Remove a tab
/// @param tabbar TabBar widget
/// @param tab Tab to remove
void vg_tabbar_remove_tab(vg_tabbar_t* tabbar, vg_tab_t* tab);

/// @brief Set active tab
/// @param tabbar TabBar widget
/// @param tab Tab to activate
void vg_tabbar_set_active(vg_tabbar_t* tabbar, vg_tab_t* tab);

/// @brief Get active tab
/// @param tabbar TabBar widget
/// @return Active tab or NULL
vg_tab_t* vg_tabbar_get_active(vg_tabbar_t* tabbar);

/// @brief Set tab title
/// @param tab Tab
/// @param title New title
void vg_tab_set_title(vg_tab_t* tab, const char* title);

/// @brief Set tab modified state
/// @param tab Tab
/// @param modified Modified state
void vg_tab_set_modified(vg_tab_t* tab, bool modified);

/// @brief Set tab user data
/// @param tab Tab
/// @param data User data
void vg_tab_set_data(vg_tab_t* tab, void* data);

/// @brief Set font for tab bar
/// @param tabbar TabBar widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_tabbar_set_font(vg_tabbar_t* tabbar, vg_font_t* font, float size);

//=============================================================================
// SplitPane Widget
//=============================================================================

/// @brief Split direction
typedef enum vg_split_direction {
    VG_SPLIT_HORIZONTAL,         ///< Left/Right split
    VG_SPLIT_VERTICAL            ///< Top/Bottom split
} vg_split_direction_t;

/// @brief SplitPane widget structure
typedef struct vg_splitpane {
    vg_widget_t base;

    vg_split_direction_t direction; ///< Split direction
    float split_position;        ///< Splitter position (0-1 ratio)
    float min_first_size;        ///< Minimum size for first pane
    float min_second_size;       ///< Minimum size for second pane
    float splitter_size;         ///< Splitter bar thickness

    uint32_t splitter_color;     ///< Splitter bar color
    uint32_t splitter_hover_color; ///< Splitter hover color

    // State
    bool splitter_hovered;       ///< Is splitter hovered
    bool dragging;               ///< Is dragging splitter
    float drag_start;            ///< Drag start position
    float drag_start_split;      ///< Split position at drag start
} vg_splitpane_t;

/// @brief Create a new split pane widget
/// @param parent Parent widget (can be NULL)
/// @param direction Split direction
/// @return New split pane widget or NULL on failure
vg_splitpane_t* vg_splitpane_create(vg_widget_t* parent, vg_split_direction_t direction);

/// @brief Set split position
/// @param split SplitPane widget
/// @param position Split position (0-1 ratio)
void vg_splitpane_set_position(vg_splitpane_t* split, float position);

/// @brief Get split position
/// @param split SplitPane widget
/// @return Split position (0-1 ratio)
float vg_splitpane_get_position(vg_splitpane_t* split);

/// @brief Set minimum pane sizes
/// @param split SplitPane widget
/// @param min_first Minimum size for first pane
/// @param min_second Minimum size for second pane
void vg_splitpane_set_min_sizes(vg_splitpane_t* split, float min_first, float min_second);

/// @brief Get first pane (for adding content)
/// @param split SplitPane widget
/// @return First pane widget
vg_widget_t* vg_splitpane_get_first(vg_splitpane_t* split);

/// @brief Get second pane (for adding content)
/// @param split SplitPane widget
/// @return Second pane widget
vg_widget_t* vg_splitpane_get_second(vg_splitpane_t* split);

//=============================================================================
// MenuBar Widget
//=============================================================================

/// @brief Menu item structure
typedef struct vg_menu_item {
    const char* text;            ///< Item text (owned)
    const char* shortcut;        ///< Keyboard shortcut text (owned)
    void (*action)(void* data);  ///< Action callback
    void* action_data;           ///< Action data
    bool enabled;                ///< Is item enabled
    bool checked;                ///< Is item checked (for toggles)
    bool separator;              ///< Is this a separator
    struct vg_menu* submenu;     ///< Submenu (if any)
    struct vg_menu_item* next;
    struct vg_menu_item* prev;
} vg_menu_item_t;

/// @brief Menu structure
typedef struct vg_menu {
    const char* title;           ///< Menu title (owned)
    vg_menu_item_t* first_item;
    vg_menu_item_t* last_item;
    int item_count;
    struct vg_menu* next;
    struct vg_menu* prev;
    bool open;                   ///< Is menu currently open
} vg_menu_t;

/// @brief MenuBar widget structure
typedef struct vg_menubar {
    vg_widget_t base;

    vg_menu_t* first_menu;       ///< First menu
    vg_menu_t* last_menu;        ///< Last menu
    int menu_count;              ///< Number of menus
    vg_menu_t* open_menu;        ///< Currently open menu
    vg_menu_item_t* highlighted; ///< Currently highlighted item

    vg_font_t* font;             ///< Font for rendering
    float font_size;             ///< Font size

    // Appearance
    float height;                ///< Menu bar height
    float menu_padding;          ///< Horizontal padding for menu titles
    float item_padding;          ///< Padding for menu items
    uint32_t bg_color;           ///< Background color
    uint32_t text_color;         ///< Text color
    uint32_t highlight_bg;       ///< Highlighted item background
    uint32_t disabled_color;     ///< Disabled item text color

    // State
    bool menu_active;            ///< Is any menu active
} vg_menubar_t;

/// @brief Create a new menu bar widget
/// @param parent Parent widget (can be NULL)
/// @return New menu bar widget or NULL on failure
vg_menubar_t* vg_menubar_create(vg_widget_t* parent);

/// @brief Add a menu to the menu bar
/// @param menubar MenuBar widget
/// @param title Menu title
/// @return New menu or NULL on failure
vg_menu_t* vg_menubar_add_menu(vg_menubar_t* menubar, const char* title);

/// @brief Add an item to a menu
/// @param menu Menu
/// @param text Item text
/// @param shortcut Keyboard shortcut (optional)
/// @param action Action callback
/// @param data Action data
/// @return New item or NULL on failure
vg_menu_item_t* vg_menu_add_item(vg_menu_t* menu, const char* text,
                                  const char* shortcut, void (*action)(void*), void* data);

/// @brief Add a separator to a menu
/// @param menu Menu
/// @return New separator item or NULL on failure
vg_menu_item_t* vg_menu_add_separator(vg_menu_t* menu);

/// @brief Add a submenu
/// @param menu Parent menu
/// @param title Submenu title
/// @return New submenu or NULL on failure
vg_menu_t* vg_menu_add_submenu(vg_menu_t* menu, const char* title);

/// @brief Set menu item enabled state
/// @param item Menu item
/// @param enabled Enabled state
void vg_menu_item_set_enabled(vg_menu_item_t* item, bool enabled);

/// @brief Set menu item checked state
/// @param item Menu item
/// @param checked Checked state
void vg_menu_item_set_checked(vg_menu_item_t* item, bool checked);

/// @brief Set font for menu bar
/// @param menubar MenuBar widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_menubar_set_font(vg_menubar_t* menubar, vg_font_t* font, float size);

//=============================================================================
// CodeEditor Widget
//=============================================================================

/// @brief Line information
typedef struct vg_code_line {
    char* text;                  ///< Line text (owned)
    size_t length;               ///< Text length
    size_t capacity;             ///< Buffer capacity
    uint32_t* colors;            ///< Per-character colors (owned, optional)
    bool modified;               ///< Line modified since last save
} vg_code_line_t;

/// @brief Selection range
typedef struct vg_selection {
    int start_line;
    int start_col;
    int end_line;
    int end_col;
} vg_selection_t;

/// @brief Syntax highlighter callback
typedef void (*vg_syntax_callback_t)(vg_widget_t* editor, int line_num,
                                      const char* text, uint32_t* colors,
                                      void* user_data);

/// @brief CodeEditor widget structure
typedef struct vg_codeeditor {
    vg_widget_t base;

    // Document
    vg_code_line_t* lines;       ///< Array of lines
    int line_count;              ///< Number of lines
    int line_capacity;           ///< Allocated capacity

    // Cursor and selection
    int cursor_line;             ///< Cursor line (0-based)
    int cursor_col;              ///< Cursor column (0-based)
    vg_selection_t selection;    ///< Current selection
    bool has_selection;          ///< Is there an active selection

    // Scroll
    float scroll_x;              ///< Horizontal scroll
    float scroll_y;              ///< Vertical scroll
    int visible_first_line;      ///< First visible line
    int visible_line_count;      ///< Number of visible lines

    // Font
    vg_font_t* font;             ///< Monospace font
    float font_size;             ///< Font size
    float char_width;            ///< Character width (monospace)
    float line_height;           ///< Line height

    // Gutter
    bool show_line_numbers;      ///< Show line number gutter
    float gutter_width;          ///< Gutter width
    uint32_t gutter_bg;          ///< Gutter background color
    uint32_t line_number_color;  ///< Line number color

    // Appearance
    uint32_t bg_color;           ///< Background color
    uint32_t text_color;         ///< Default text color
    uint32_t cursor_color;       ///< Cursor color
    uint32_t selection_color;    ///< Selection color
    uint32_t current_line_bg;    ///< Current line highlight

    // Syntax highlighting
    vg_syntax_callback_t syntax_highlighter;
    void* syntax_data;

    // Editing options
    bool read_only;              ///< Read-only mode
    bool insert_mode;            ///< Insert vs overwrite mode
    int tab_width;               ///< Tab width in spaces
    bool use_spaces;             ///< Use spaces for tabs
    bool auto_indent;            ///< Auto-indent on enter
    bool word_wrap;              ///< Word wrapping

    // State
    bool cursor_visible;         ///< Cursor blink state
    float cursor_blink_time;     ///< Cursor blink timer
    bool modified;               ///< Document modified since last save
} vg_codeeditor_t;

/// @brief Create a new code editor widget
/// @param parent Parent widget (can be NULL)
/// @return New code editor widget or NULL on failure
vg_codeeditor_t* vg_codeeditor_create(vg_widget_t* parent);

/// @brief Set editor text content
/// @param editor CodeEditor widget
/// @param text New text content
void vg_codeeditor_set_text(vg_codeeditor_t* editor, const char* text);

/// @brief Get editor text content
/// @param editor CodeEditor widget
/// @return Text content (caller must free)
char* vg_codeeditor_get_text(vg_codeeditor_t* editor);

/// @brief Get selected text
/// @param editor CodeEditor widget
/// @return Selected text (caller must free) or NULL if no selection
char* vg_codeeditor_get_selection(vg_codeeditor_t* editor);

/// @brief Set cursor position
/// @param editor CodeEditor widget
/// @param line Line number (0-based)
/// @param col Column number (0-based)
void vg_codeeditor_set_cursor(vg_codeeditor_t* editor, int line, int col);

/// @brief Get cursor position
/// @param editor CodeEditor widget
/// @param out_line Output line number
/// @param out_col Output column number
void vg_codeeditor_get_cursor(vg_codeeditor_t* editor, int* out_line, int* out_col);

/// @brief Set selection range
/// @param editor CodeEditor widget
/// @param start_line Selection start line
/// @param start_col Selection start column
/// @param end_line Selection end line
/// @param end_col Selection end column
void vg_codeeditor_set_selection(vg_codeeditor_t* editor,
                                  int start_line, int start_col,
                                  int end_line, int end_col);

/// @brief Insert text at cursor
/// @param editor CodeEditor widget
/// @param text Text to insert
void vg_codeeditor_insert_text(vg_codeeditor_t* editor, const char* text);

/// @brief Delete selected text
/// @param editor CodeEditor widget
void vg_codeeditor_delete_selection(vg_codeeditor_t* editor);

/// @brief Scroll to line
/// @param editor CodeEditor widget
/// @param line Line number (0-based)
void vg_codeeditor_scroll_to_line(vg_codeeditor_t* editor, int line);

/// @brief Set syntax highlighter
/// @param editor CodeEditor widget
/// @param callback Syntax highlight callback
/// @param user_data User data
void vg_codeeditor_set_syntax(vg_codeeditor_t* editor,
                               vg_syntax_callback_t callback, void* user_data);

/// @brief Undo last action
/// @param editor CodeEditor widget
void vg_codeeditor_undo(vg_codeeditor_t* editor);

/// @brief Redo last undone action
/// @param editor CodeEditor widget
void vg_codeeditor_redo(vg_codeeditor_t* editor);

/// @brief Set font for code editor
/// @param editor CodeEditor widget
/// @param font Monospace font to use
/// @param size Font size in pixels
void vg_codeeditor_set_font(vg_codeeditor_t* editor, vg_font_t* font, float size);

/// @brief Get line count
/// @param editor CodeEditor widget
/// @return Number of lines
int vg_codeeditor_get_line_count(vg_codeeditor_t* editor);

/// @brief Check if document is modified
/// @param editor CodeEditor widget
/// @return True if modified
bool vg_codeeditor_is_modified(vg_codeeditor_t* editor);

/// @brief Clear modified flag
/// @param editor CodeEditor widget
void vg_codeeditor_clear_modified(vg_codeeditor_t* editor);

#ifdef __cplusplus
}
#endif

#endif // VG_IDE_WIDGETS_H
