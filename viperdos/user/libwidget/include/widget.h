/**
 * @file widget.h
 * @brief Widget toolkit API for ViperDOS GUI applications.
 *
 * @details
 * This header defines a comprehensive widget toolkit for building graphical
 * user interfaces in ViperDOS. The toolkit provides a hierarchical widget
 * system inspired by classic desktop GUI frameworks, with an Amiga Workbench
 * visual style.
 *
 * ## Architecture Overview
 *
 * The widget system is built around a base `widget_t` structure that all
 * specialized widgets (buttons, labels, textboxes, etc.) extend. Widgets
 * form a tree hierarchy where each widget can have a parent and children.
 * Events propagate through this hierarchy, and painting occurs top-down.
 *
 * ## Widget Types
 *
 * - **Containers**: Generic parent widgets for grouping children
 * - **Buttons**: Clickable buttons with text labels and 3D styling
 * - **Labels**: Static text display with alignment options
 * - **TextBoxes**: Single or multi-line text input fields
 * - **Checkboxes**: Toggle controls with text labels
 * - **ListViews**: Scrollable lists of selectable items
 * - **TreeViews**: Hierarchical expandable/collapsible trees
 * - **Menus**: Popup menus with items, separators, and submenus
 * - **ProgressBars**: Visual progress indicators
 * - **Scrollbars**: Horizontal or vertical scroll controls
 *
 * ## Layout System
 *
 * The toolkit includes a flexible layout system supporting:
 * - **Manual positioning**: Direct x,y coordinate placement
 * - **Horizontal layout**: Left-to-right arrangement
 * - **Vertical layout**: Top-to-bottom arrangement
 * - **Grid layout**: Row/column grid arrangement
 * - **Border layout**: North/South/East/West/Center regions
 *
 * ## Visual Style
 *
 * The toolkit implements Amiga Workbench 3.x visual conventions:
 * - 3D raised and sunken borders
 * - Gray color palette with blue/orange accents
 * - Consistent button and frame styling
 *
 * ## Usage Example
 *
 * ```c
 * // Create application window
 * widget_app_t *app = widget_app_create("My App", 400, 300);
 *
 * // Create root container with vertical layout
 * widget_t *root = widget_create(WIDGET_CONTAINER, NULL);
 * layout_t *layout = layout_create(LAYOUT_VERTICAL);
 * layout_set_spacing(layout, 8);
 * widget_set_layout(root, layout);
 *
 * // Add a label
 * label_t *lbl = label_create(&root->base, "Hello, World!");
 *
 * // Add a button
 * button_t *btn = button_create(&root->base, "Click Me");
 * button_set_onclick(btn, on_button_click, NULL);
 *
 * // Run the application
 * widget_app_set_root(app, root);
 * widget_app_run(app);
 * ```
 *
 * @note This toolkit requires libgui for low-level window and drawing operations.
 *
 * @see gui.h for the underlying graphics API
 *
 * Part of the Viper project, under the GNU GPL v3.
 * See LICENSE for license information.
 */

#ifndef VIPER_WIDGET_H
#define VIPER_WIDGET_H

#include <gui.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Widget Type Enumeration
 * ============================================================================ */

/**
 * @brief Enumeration of all supported widget types.
 *
 * @details
 * Each widget in the toolkit has a type identifier that determines its
 * behavior, rendering, and event handling. The type is stored in the
 * base widget_t structure and is set during widget creation.
 *
 * Specialized widget types (WIDGET_BUTTON, WIDGET_LABEL, etc.) have
 * corresponding structures that extend the base widget_t with type-specific
 * fields. Use the appropriate creation function (button_create, label_create,
 * etc.) to instantiate these widgets.
 */
typedef enum {
    WIDGET_CONTAINER,   /**< Generic container for grouping child widgets */
    WIDGET_BUTTON,      /**< Clickable button with text label */
    WIDGET_LABEL,       /**< Static text display */
    WIDGET_TEXTBOX,     /**< Text input field (single or multi-line) */
    WIDGET_CHECKBOX,    /**< Toggle checkbox with text label */
    WIDGET_LISTVIEW,    /**< Scrollable list of selectable items */
    WIDGET_TREEVIEW,    /**< Hierarchical tree with expandable nodes */
    WIDGET_MENU,        /**< Popup menu (internal use) */
    WIDGET_MENUITEM,    /**< Menu item (internal use) */
    WIDGET_TOOLBAR,     /**< Horizontal toolbar container (reserved) */
    WIDGET_STATUSBAR,   /**< Status bar at window bottom (reserved) */
    WIDGET_PANEL,       /**< Styled panel container (reserved) */
    WIDGET_SCROLLBAR,   /**< Scroll control (horizontal or vertical) */
    WIDGET_COMBOBOX,    /**< Drop-down selection box (reserved) */
    WIDGET_PROGRESSBAR, /**< Progress indicator bar */
} widget_type_t;

/**
 * @brief Layout manager types for automatic widget positioning.
 *
 * @details
 * Layout managers automatically position child widgets within a container
 * based on rules defined by the layout type. This eliminates the need for
 * manual coordinate calculations and enables responsive UI design.
 *
 * To use a layout:
 * 1. Create a layout with layout_create()
 * 2. Configure spacing/margins with layout_set_*() functions
 * 3. Attach to a container with widget_set_layout()
 * 4. Add children to the container
 * 5. Call layout_apply() to position children
 */
typedef enum {
    LAYOUT_NONE,       /**< No automatic layout; widgets positioned manually */
    LAYOUT_HORIZONTAL, /**< Arrange children left-to-right in a row */
    LAYOUT_VERTICAL,   /**< Arrange children top-to-bottom in a column */
    LAYOUT_GRID,       /**< Arrange children in a row/column grid */
    LAYOUT_BORDER,     /**< Five-region layout: North, South, East, West, Center */
} layout_type_t;

/**
 * @brief Text alignment options for labels and other text widgets.
 */
typedef enum {
    ALIGN_LEFT = 0,   /**< Align text to the left edge */
    ALIGN_CENTER = 1, /**< Center text horizontally */
    ALIGN_RIGHT = 2,  /**< Align text to the right edge */
} alignment_t;

/**
 * @brief Constraint values for LAYOUT_BORDER positioning.
 *
 * @details
 * When using border layout, each child widget must be assigned a region
 * constraint that determines where it will be placed:
 *
 * ```
 * +----------------------------+
 * |          NORTH             |
 * +------+-------------+-------+
 * |      |             |       |
 * | WEST |   CENTER    | EAST  |
 * |      |             |       |
 * +------+-------------+-------+
 * |          SOUTH             |
 * +----------------------------+
 * ```
 *
 * Use widget_set_layout_constraint() to assign a region to a widget.
 */
typedef enum {
    BORDER_NORTH = 0,  /**< Top edge, full width */
    BORDER_SOUTH = 1,  /**< Bottom edge, full width */
    BORDER_EAST = 2,   /**< Right edge, between North and South */
    BORDER_WEST = 3,   /**< Left edge, between North and South */
    BORDER_CENTER = 4, /**< Remaining space in the middle */
} border_constraint_t;

/* ============================================================================
 * Amiga Workbench Color Palette
 * ============================================================================ */

/**
 * @defgroup wb_colors Workbench Colors
 * @brief Standard Amiga Workbench 3.x color palette.
 *
 * @details
 * These colors replicate the classic Amiga Workbench visual style.
 * Use these constants for consistent appearance across all widgets.
 *
 * Colors are in 0xAARRGGBB format (alpha, red, green, blue).
 * @{
 */
#define WB_GRAY_LIGHT 0xFFAAAAAA /**< Light gray for backgrounds */
#define WB_GRAY_MED   0xFF888888 /**< Medium gray for disabled items */
#define WB_GRAY_DARK  0xFF555555 /**< Dark gray for shadows/borders */
#define WB_BLUE       0xFF0055AA /**< Workbench blue for selections */
#define WB_ORANGE     0xFFFF8800 /**< Orange for highlights/selections */
#define WB_WHITE      0xFFFFFFFF /**< White for highlights and text */
#define WB_BLACK      0xFF000000 /**< Black for text and outlines */
#define WB_RED        0xFFFF4444 /**< Red for errors/warnings */
#define WB_GREEN      0xFF00AA44 /**< Green for success indicators */
/** @} */

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct widget widget_t;
typedef struct layout layout_t;
typedef struct button button_t;
typedef struct label label_t;
typedef struct textbox textbox_t;
typedef struct checkbox checkbox_t;
typedef struct listview listview_t;
typedef struct treeview treeview_t;
typedef struct tree_node tree_node_t;
typedef struct menu menu_t;
typedef struct menu_item menu_item_t;
typedef struct progressbar progressbar_t;
typedef struct scrollbar scrollbar_t;

/* ============================================================================
 * Callback Function Types
 * ============================================================================ */

/**
 * @brief Callback for custom widget painting.
 * @param w   The widget being painted.
 * @param win The GUI window to draw into.
 */
typedef void (*widget_paint_fn)(widget_t *w, gui_window_t *win);

/**
 * @brief Callback for mouse click events on a widget.
 * @param w      The widget that was clicked.
 * @param x      X coordinate of click relative to widget.
 * @param y      Y coordinate of click relative to widget.
 * @param button Mouse button (0=left, 1=right, 2=middle).
 */
typedef void (*widget_click_fn)(widget_t *w, int x, int y, int button);

/**
 * @brief Callback for keyboard events on a widget.
 * @param w       The widget receiving the key event.
 * @param keycode Raw keycode from the input system.
 * @param ch      Character representation (0 if not printable).
 */
typedef void (*widget_key_fn)(widget_t *w, int keycode, char ch);

/**
 * @brief Callback for focus change events.
 * @param w      The widget whose focus state changed.
 * @param gained True if focus was gained, false if lost.
 */
typedef void (*widget_focus_fn)(widget_t *w, bool gained);

/**
 * @brief Generic callback with user data pointer.
 * @param user_data Application-defined data passed during callback registration.
 */
typedef void (*widget_callback_fn)(void *user_data);

/**
 * @brief Callback for listview item selection events.
 * @param index     Index of the selected item.
 * @param user_data Application-defined data.
 */
typedef void (*listview_select_fn)(int index, void *user_data);

/**
 * @brief Callback for treeview node selection/expansion events.
 * @param node      The tree node that was selected or expanded.
 * @param user_data Application-defined data.
 */
typedef void (*treeview_select_fn)(tree_node_t *node, void *user_data);

/* ============================================================================
 * Base Widget Structure
 * ============================================================================ */

/**
 * @brief Base structure for all widgets in the toolkit.
 *
 * @details
 * The widget_t structure contains common fields shared by all widget types:
 * - Type identification
 * - Parent/child relationships for hierarchy
 * - Geometry (position and size)
 * - State flags (visible, enabled, focused)
 * - Colors for foreground and background
 * - Event callback function pointers
 * - Layout information
 *
 * Specialized widgets (button_t, label_t, etc.) embed this structure as their
 * first member, enabling polymorphic behavior through casting.
 *
 * @note Do not instantiate this structure directly for specialized widgets.
 *       Use the appropriate creation function (button_create, etc.) instead.
 */
struct widget {
    widget_type_t type;     /**< Widget type identifier */
    widget_t *parent;       /**< Parent widget (NULL for root) */
    widget_t **children;    /**< Array of child widget pointers */
    int child_count;        /**< Number of children currently attached */
    int child_capacity;     /**< Allocated capacity for children array */

    int x, y;               /**< Position relative to parent (pixels) */
    int width, height;      /**< Size of the widget (pixels) */
    bool visible;           /**< Whether widget is rendered */
    bool enabled;           /**< Whether widget accepts input */
    bool focused;           /**< Whether widget has keyboard focus */

    uint32_t bg_color;      /**< Background fill color (0xAARRGGBB) */
    uint32_t fg_color;      /**< Foreground/text color (0xAARRGGBB) */

    /* Event callback function pointers */
    widget_paint_fn on_paint;   /**< Custom paint handler (optional) */
    widget_click_fn on_click;   /**< Mouse click handler (optional) */
    widget_key_fn on_key;       /**< Keyboard input handler (optional) */
    widget_focus_fn on_focus;   /**< Focus change handler (optional) */
    void *user_data;            /**< Application-defined data pointer */

    /* Layout information */
    layout_t *layout;           /**< Layout manager for children (optional) */
    int layout_constraint;      /**< Constraint for border layout */
};

/* ============================================================================
 * Layout Structure
 * ============================================================================ */

/**
 * @brief Configuration for automatic widget layout.
 *
 * @details
 * A layout structure defines how child widgets are automatically positioned
 * within their parent container. Create with layout_create(), configure with
 * setter functions, then attach to a widget with widget_set_layout().
 *
 * @see layout_create, layout_set_spacing, layout_set_margins, layout_apply
 */
struct layout {
    layout_type_t type;      /**< Type of layout algorithm to use */
    int spacing;             /**< Space between adjacent children (pixels) */
    int margin_left;         /**< Left edge margin (pixels) */
    int margin_top;          /**< Top edge margin (pixels) */
    int margin_right;        /**< Right edge margin (pixels) */
    int margin_bottom;       /**< Bottom edge margin (pixels) */
    int columns;             /**< Number of columns for LAYOUT_GRID */
    int rows;                /**< Number of rows for LAYOUT_GRID */
};

/* ============================================================================
 * Button Widget
 * ============================================================================ */

/**
 * @brief Clickable button widget with text label.
 *
 * @details
 * Buttons are interactive widgets that trigger a callback when clicked.
 * They render with 3D raised/sunken appearance following Amiga style.
 *
 * Visual states:
 * - Normal: Raised 3D appearance with light top/left edges
 * - Pressed: Sunken 3D appearance while mouse button is held
 * - Hovered: Optional highlight effect (implementation dependent)
 * - Disabled: Grayed out appearance, no interaction
 *
 * @see button_create, button_set_onclick
 */
struct button {
    widget_t base;              /**< Base widget (must be first member) */
    char text[64];              /**< Button label text */
    bool pressed;               /**< True while mouse button is held down */
    bool hovered;               /**< True while mouse is over the button */
    widget_callback_fn on_click; /**< Callback invoked when button is clicked */
    void *callback_data;        /**< User data passed to click callback */
};

/* ============================================================================
 * Label Widget
 * ============================================================================ */

/**
 * @brief Static text display widget.
 *
 * @details
 * Labels display non-interactive text with configurable alignment.
 * They are commonly used for form field labels, status messages,
 * and informational text throughout the UI.
 *
 * Labels support horizontal alignment (left, center, right) but
 * do not currently support multi-line text or word wrapping.
 *
 * @see label_create, label_set_text, label_set_alignment
 */
struct label {
    widget_t base;        /**< Base widget (must be first member) */
    char text[256];       /**< Text content to display */
    alignment_t alignment; /**< Horizontal text alignment */
};

/* ============================================================================
 * TextBox Widget
 * ============================================================================ */

/**
 * @brief Text input field widget.
 *
 * @details
 * TextBox provides a text entry field with editing capabilities including:
 * - Cursor positioning and movement
 * - Text selection (start and end positions)
 * - Horizontal scrolling for long text
 * - Optional password mode (displays asterisks)
 * - Optional multi-line mode (reserved for future)
 * - Optional read-only mode
 *
 * The text buffer is dynamically allocated and grows as needed.
 *
 * @see textbox_create, textbox_set_text, textbox_get_text
 */
struct textbox {
    widget_t base;              /**< Base widget (must be first member) */
    char *text;                 /**< Dynamically allocated text buffer */
    int text_capacity;          /**< Allocated buffer size */
    int text_length;            /**< Current text length (not including null) */
    int cursor_pos;             /**< Cursor position (0 = before first char) */
    int scroll_offset;          /**< Horizontal scroll offset for long text */
    int selection_start;        /**< Selection start position (-1 if none) */
    int selection_end;          /**< Selection end position */
    bool password_mode;         /**< If true, display asterisks instead of text */
    bool multiline;             /**< If true, allow multiple lines (reserved) */
    bool readonly;              /**< If true, prevent editing */
    widget_callback_fn on_change; /**< Callback when text content changes */
    widget_callback_fn on_enter;  /**< Callback when Enter key is pressed */
    void *callback_data;        /**< User data for callbacks */
};

/* ============================================================================
 * Checkbox Widget
 * ============================================================================ */

/**
 * @brief Toggle checkbox widget with text label.
 *
 * @details
 * Checkboxes provide a boolean on/off control with an associated text label.
 * Clicking anywhere on the checkbox or its label toggles the state.
 *
 * The checkbox renders as a small square box with a checkmark when checked,
 * followed by the label text to the right.
 *
 * @see checkbox_create, checkbox_set_checked, checkbox_is_checked
 */
struct checkbox {
    widget_t base;              /**< Base widget (must be first member) */
    char text[64];              /**< Label text displayed next to checkbox */
    bool checked;               /**< Current checked state */
    widget_callback_fn on_change; /**< Callback when checked state changes */
    void *callback_data;        /**< User data for callback */
};

/* ============================================================================
 * ListView Widget
 * ============================================================================ */

/**
 * @brief Scrollable list of selectable text items.
 *
 * @details
 * ListView displays a vertical list of text items that can be selected
 * by clicking. Features include:
 * - Automatic vertical scrolling for long lists
 * - Single selection mode (one item at a time)
 * - Optional multi-selection mode (multiple items)
 * - Selection and double-click callbacks
 * - Dynamic item addition, insertion, and removal
 *
 * Items are stored as dynamically allocated strings. The selected item
 * is highlighted with the selection color (typically WB_BLUE or WB_ORANGE).
 *
 * @see listview_create, listview_add_item, listview_get_selected
 */
struct listview {
    widget_t base;              /**< Base widget (must be first member) */
    char **items;               /**< Array of item strings */
    int item_count;             /**< Number of items in the list */
    int item_capacity;          /**< Allocated capacity for items array */
    int selected_index;         /**< Currently selected item (-1 if none) */
    int scroll_offset;          /**< Vertical scroll offset in items */
    int visible_items;          /**< Number of items visible without scrolling */
    bool multi_select;          /**< Whether multiple selection is allowed */
    bool *selected;             /**< Selection state array for multi-select */
    listview_select_fn on_select;      /**< Callback for selection changes */
    listview_select_fn on_double_click; /**< Callback for double-clicks */
    void *callback_data;        /**< User data for callbacks */
};

/* ============================================================================
 * TreeView Widget
 * ============================================================================ */

/**
 * @brief Node in a hierarchical tree structure.
 *
 * @details
 * Tree nodes form the hierarchical data structure displayed by treeview
 * widgets. Each node has:
 * - Text label displayed in the tree
 * - Optional children forming subtrees
 * - Expanded/collapsed state for nodes with children
 * - User data pointer for application-specific data
 *
 * Nodes with children display an expand/collapse indicator. Clicking
 * the indicator toggles the expanded state, showing or hiding children.
 *
 * @see treeview_add_node, tree_node_set_text, tree_node_get_user_data
 */
struct tree_node {
    char text[64];              /**< Node label text */
    tree_node_t *children;      /**< Array of child nodes */
    int child_count;            /**< Number of children */
    int child_capacity;         /**< Allocated capacity for children array */
    tree_node_t *parent;        /**< Parent node (NULL for root) */
    bool expanded;              /**< Whether children are visible */
    void *user_data;            /**< Application-defined data */
};

/**
 * @brief Hierarchical tree display widget.
 *
 * @details
 * TreeView displays hierarchical data as an expandable/collapsible tree.
 * Features include:
 * - Nested nodes with arbitrary depth
 * - Expand/collapse controls for nodes with children
 * - Single node selection with callback
 * - Vertical scrolling for large trees
 * - Indentation to show hierarchy level
 *
 * The tree starts with a hidden root node. Add visible nodes as children
 * of the root using treeview_add_node(tv, NULL, "text").
 *
 * @see treeview_create, treeview_add_node, treeview_expand, treeview_collapse
 */
struct treeview {
    widget_t base;              /**< Base widget (must be first member) */
    tree_node_t *root;          /**< Hidden root node of the tree */
    tree_node_t *selected;      /**< Currently selected node (NULL if none) */
    int scroll_offset;          /**< Vertical scroll offset in visible nodes */
    int visible_items;          /**< Number of nodes visible without scrolling */
    treeview_select_fn on_select; /**< Callback for selection changes */
    treeview_select_fn on_expand; /**< Callback for expand/collapse events */
    void *callback_data;        /**< User data for callbacks */
};

/* ============================================================================
 * Menu Structures
 * ============================================================================ */

/**
 * @brief Single item within a popup menu.
 *
 * @details
 * Menu items can be:
 * - Regular clickable items with text and optional shortcut hint
 * - Separators (horizontal lines between groups)
 * - Checkable items (toggle state)
 * - Submenu triggers (opens a nested menu)
 *
 * Items can be enabled or disabled. Disabled items are grayed out
 * and do not respond to clicks.
 */
struct menu_item {
    char text[64];              /**< Item label text */
    char shortcut[16];          /**< Keyboard shortcut hint (e.g., "Ctrl+S") */
    bool separator;             /**< If true, item is a separator line */
    bool checked;               /**< Checkmark state for toggle items */
    bool enabled;               /**< Whether item is interactive */
    menu_t *submenu;            /**< Submenu to open (NULL for leaf items) */
    widget_callback_fn on_click; /**< Callback when item is clicked */
    void *callback_data;        /**< User data for callback */
};

/**
 * @brief Popup menu containing menu items.
 *
 * @details
 * Menus are displayed as floating popup windows containing a vertical
 * list of menu items. They support:
 * - Text items with click callbacks
 * - Keyboard shortcut hints
 * - Separator lines between groups
 * - Nested submenus
 * - Checkable items
 * - Enabled/disabled states
 *
 * Show a menu with menu_show() and hide with menu_hide(). The menu
 * handles its own mouse interaction and closes automatically when
 * an item is clicked or when clicking outside the menu.
 *
 * @see menu_create, menu_add_item, menu_show
 */
struct menu {
    menu_item_t *items;         /**< Array of menu items */
    int item_count;             /**< Number of items in the menu */
    int item_capacity;          /**< Allocated capacity for items array */
    bool visible;               /**< Whether menu is currently displayed */
    int x, y;                   /**< Screen position when visible */
    int width, height;          /**< Calculated menu dimensions */
    int hovered_index;          /**< Currently hovered item (-1 if none) */
};

/* ============================================================================
 * ProgressBar Widget
 * ============================================================================ */

/**
 * @brief Visual progress indicator bar.
 *
 * @details
 * ProgressBar displays a horizontal bar that fills to indicate progress
 * toward completion. Features:
 * - Configurable min/max range (default 0-100)
 * - Current value shown as filled portion of bar
 * - Optional percentage text overlay
 * - Amiga-style 3D border appearance
 *
 * Typically used for file operations, loading screens, or any task
 * with measurable progress.
 *
 * @see progressbar_create, progressbar_set_value, progressbar_set_range
 */
struct progressbar {
    widget_t base;      /**< Base widget (must be first member) */
    int value;          /**< Current progress value */
    int min_val;        /**< Minimum value (left edge of bar) */
    int max_val;        /**< Maximum value (right edge of bar) */
    bool show_text;     /**< Whether to display percentage text */
};

/* ============================================================================
 * Scrollbar Widget
 * ============================================================================ */

/**
 * @brief Scroll control for scrollable content.
 *
 * @details
 * Scrollbar provides a draggable thumb within a track to control
 * scrolling of associated content. Can be oriented horizontally
 * or vertically. Features:
 * - Configurable min/max scroll range
 * - Page size affects thumb size proportionally
 * - Click in track to page up/down
 * - Drag thumb for continuous scrolling
 * - Change callback for scroll position updates
 *
 * @see scrollbar_create, scrollbar_set_value, scrollbar_set_range
 */
struct scrollbar {
    widget_t base;              /**< Base widget (must be first member) */
    bool vertical;              /**< True for vertical, false for horizontal */
    int value;                  /**< Current scroll position */
    int min_val;                /**< Minimum scroll position */
    int max_val;                /**< Maximum scroll position */
    int page_size;              /**< Visible portion size (affects thumb size) */
    widget_callback_fn on_change; /**< Callback when value changes */
    void *callback_data;        /**< User data for callback */
};

/* ============================================================================
 * Message Box Types
 * ============================================================================ */

/**
 * @brief Button configuration for message box dialogs.
 */
typedef enum {
    MB_OK,            /**< Single "OK" button */
    MB_OK_CANCEL,     /**< "OK" and "Cancel" buttons */
    MB_YES_NO,        /**< "Yes" and "No" buttons */
    MB_YES_NO_CANCEL, /**< "Yes", "No", and "Cancel" buttons */
} msgbox_type_t;

/**
 * @brief Icon type for message box dialogs.
 */
typedef enum {
    MB_ICON_INFO,     /**< Information icon (i) */
    MB_ICON_WARNING,  /**< Warning icon (!) */
    MB_ICON_ERROR,    /**< Error icon (X) */
    MB_ICON_QUESTION, /**< Question icon (?) */
} msgbox_icon_t;

/**
 * @brief Result codes returned by message box dialogs.
 */
typedef enum {
    MB_RESULT_OK = 1,     /**< User clicked OK */
    MB_RESULT_CANCEL = 2, /**< User clicked Cancel or closed dialog */
    MB_RESULT_YES = 3,    /**< User clicked Yes */
    MB_RESULT_NO = 4,     /**< User clicked No */
} msgbox_result_t;

/* ============================================================================
 * Core Widget Functions
 * ============================================================================ */

/**
 * @brief Create a new widget of the specified type.
 *
 * @details
 * Allocates and initializes a widget structure with default values.
 * If a parent is provided, the new widget is automatically added
 * as a child of the parent.
 *
 * For specialized widgets (buttons, labels, etc.), use the specific
 * creation functions (button_create, label_create, etc.) instead.
 *
 * @param type   The type of widget to create.
 * @param parent Parent widget to attach to, or NULL for root widget.
 * @return       Pointer to the new widget, or NULL on allocation failure.
 *
 * @see widget_destroy
 */
widget_t *widget_create(widget_type_t type, widget_t *parent);

/**
 * @brief Destroy a widget and free its resources.
 *
 * @details
 * Recursively destroys all child widgets, removes the widget from its
 * parent's child list, frees any dynamically allocated memory, and
 * deallocates the widget structure itself.
 *
 * @param w The widget to destroy. May be NULL (no-op).
 *
 * @warning Do not use the widget pointer after calling this function.
 */
void widget_destroy(widget_t *w);

/**
 * @brief Set the position of a widget relative to its parent.
 *
 * @param w The widget to position.
 * @param x X coordinate in pixels from parent's left edge.
 * @param y Y coordinate in pixels from parent's top edge.
 */
void widget_set_position(widget_t *w, int x, int y);

/**
 * @brief Set the size of a widget.
 *
 * @param w      The widget to resize.
 * @param width  New width in pixels.
 * @param height New height in pixels.
 */
void widget_set_size(widget_t *w, int width, int height);

/**
 * @brief Set both position and size of a widget in one call.
 *
 * @param w      The widget to modify.
 * @param x      X coordinate relative to parent.
 * @param y      Y coordinate relative to parent.
 * @param width  Width in pixels.
 * @param height Height in pixels.
 */
void widget_set_geometry(widget_t *w, int x, int y, int width, int height);

/**
 * @brief Get the current geometry of a widget.
 *
 * @param w      The widget to query.
 * @param x      Output: X coordinate (may be NULL).
 * @param y      Output: Y coordinate (may be NULL).
 * @param width  Output: width in pixels (may be NULL).
 * @param height Output: height in pixels (may be NULL).
 */
void widget_get_geometry(widget_t *w, int *x, int *y, int *width, int *height);

/**
 * @brief Set the visibility of a widget.
 *
 * @details
 * Invisible widgets are not rendered and do not receive events.
 * Hiding a parent also hides all its children.
 *
 * @param w       The widget to modify.
 * @param visible True to show, false to hide.
 */
void widget_set_visible(widget_t *w, bool visible);

/**
 * @brief Set whether a widget accepts user input.
 *
 * @details
 * Disabled widgets are typically rendered in a grayed-out style
 * and do not respond to mouse or keyboard input.
 *
 * @param w       The widget to modify.
 * @param enabled True to enable, false to disable.
 */
void widget_set_enabled(widget_t *w, bool enabled);

/**
 * @brief Check if a widget is currently visible.
 *
 * @param w The widget to query.
 * @return  True if visible, false if hidden.
 */
bool widget_is_visible(widget_t *w);

/**
 * @brief Check if a widget is currently enabled.
 *
 * @param w The widget to query.
 * @return  True if enabled, false if disabled.
 */
bool widget_is_enabled(widget_t *w);

/**
 * @brief Set the foreground and background colors of a widget.
 *
 * @param w  The widget to modify.
 * @param fg Foreground color (text, lines) in 0xAARRGGBB format.
 * @param bg Background color in 0xAARRGGBB format.
 */
void widget_set_colors(widget_t *w, uint32_t fg, uint32_t bg);

/**
 * @brief Give keyboard focus to a widget.
 *
 * @details
 * The focused widget receives keyboard input events. Only one widget
 * can have focus at a time. This function removes focus from the
 * previously focused widget and triggers focus callbacks.
 *
 * @param w The widget to focus.
 */
void widget_set_focus(widget_t *w);

/**
 * @brief Check if a widget currently has keyboard focus.
 *
 * @param w The widget to query.
 * @return  True if focused, false otherwise.
 */
bool widget_has_focus(widget_t *w);

/**
 * @brief Add a child widget to a parent container.
 *
 * @details
 * The child's parent pointer is set to the parent, and the child
 * is added to the parent's children array. The child's position
 * is relative to the parent's client area.
 *
 * @param parent The parent container widget.
 * @param child  The child widget to add.
 */
void widget_add_child(widget_t *parent, widget_t *child);

/**
 * @brief Remove a child widget from its parent.
 *
 * @details
 * The child is removed from the parent's children array and its
 * parent pointer is set to NULL. The child is not destroyed.
 *
 * @param parent The parent container widget.
 * @param child  The child widget to remove.
 */
void widget_remove_child(widget_t *parent, widget_t *child);

/**
 * @brief Get a widget's parent.
 *
 * @param w The widget to query.
 * @return  Pointer to parent widget, or NULL if w is a root widget.
 */
widget_t *widget_get_parent(widget_t *w);

/**
 * @brief Get the number of children attached to a widget.
 *
 * @param w The widget to query.
 * @return  Number of child widgets.
 */
int widget_get_child_count(widget_t *w);

/**
 * @brief Get a child widget by index.
 *
 * @param w     The parent widget.
 * @param index Zero-based child index.
 * @return      Pointer to child widget, or NULL if index out of range.
 */
widget_t *widget_get_child(widget_t *w, int index);

/**
 * @brief Request that a widget be repainted.
 *
 * @details
 * Marks the widget as needing redraw. The actual paint operation
 * occurs during the next event loop iteration.
 *
 * @param w The widget to repaint.
 */
void widget_repaint(widget_t *w);

/**
 * @brief Paint a widget and its children to a window.
 *
 * @details
 * Renders the widget using its type-specific paint logic, then
 * recursively paints all visible children. Called internally by
 * the event loop; applications rarely need to call this directly.
 *
 * @param w   The widget to paint.
 * @param win The GUI window to paint into.
 */
void widget_paint(widget_t *w, gui_window_t *win);

/**
 * @brief Paint only the children of a widget.
 *
 * @details
 * Useful when implementing custom widget paint handlers that
 * need to paint children after drawing the widget's own content.
 *
 * @param w   The parent widget.
 * @param win The GUI window to paint into.
 */
void widget_paint_children(widget_t *w, gui_window_t *win);

/**
 * @brief Dispatch a mouse event to a widget hierarchy.
 *
 * @details
 * Routes the mouse event to the appropriate widget based on position.
 * Handles hit testing, click detection, and callback invocation.
 *
 * @param w          Root widget of the hierarchy.
 * @param x          X coordinate in window space.
 * @param y          Y coordinate in window space.
 * @param button     Mouse button (0=left, 1=right, 2=middle).
 * @param event_type Event type (0=move, 1=press, 2=release).
 * @return           True if the event was handled, false otherwise.
 */
bool widget_handle_mouse(widget_t *w, int x, int y, int button, int event_type);

/**
 * @brief Dispatch a keyboard event to the focused widget.
 *
 * @param w       Root widget (focus is determined internally).
 * @param keycode Raw keycode from input system.
 * @param ch      Character representation (0 if not printable).
 * @return        True if the event was handled, false otherwise.
 */
bool widget_handle_key(widget_t *w, int keycode, char ch);

/**
 * @brief Find the widget at a given position.
 *
 * @details
 * Performs hit testing to find the deepest widget in the hierarchy
 * that contains the specified point and is visible/enabled.
 *
 * @param root Root widget to search from.
 * @param x    X coordinate in root's coordinate space.
 * @param y    Y coordinate in root's coordinate space.
 * @return     The widget at that position, or NULL if none.
 */
widget_t *widget_find_at(widget_t *root, int x, int y);

/**
 * @brief Associate application-defined data with a widget.
 *
 * @param w    The widget.
 * @param data Pointer to application data.
 */
void widget_set_user_data(widget_t *w, void *data);

/**
 * @brief Retrieve application-defined data from a widget.
 *
 * @param w The widget.
 * @return  Previously set user data, or NULL.
 */
void *widget_get_user_data(widget_t *w);

/* ============================================================================
 * Button Functions
 * ============================================================================ */

/**
 * @brief Create a new button widget.
 *
 * @param parent Parent widget to attach to, or NULL.
 * @param text   Button label text.
 * @return       Pointer to new button, or NULL on failure.
 */
button_t *button_create(widget_t *parent, const char *text);

/**
 * @brief Set the text displayed on a button.
 *
 * @param btn  The button to modify.
 * @param text New label text (copied, max 63 chars).
 */
void button_set_text(button_t *btn, const char *text);

/**
 * @brief Get the text displayed on a button.
 *
 * @param btn The button to query.
 * @return    Pointer to button's text (do not free).
 */
const char *button_get_text(button_t *btn);

/**
 * @brief Set the click callback for a button.
 *
 * @param btn      The button to modify.
 * @param callback Function to call when button is clicked.
 * @param data     User data passed to callback.
 */
void button_set_onclick(button_t *btn, widget_callback_fn callback, void *data);

/* ============================================================================
 * Label Functions
 * ============================================================================ */

/**
 * @brief Create a new label widget.
 *
 * @param parent Parent widget to attach to, or NULL.
 * @param text   Initial text to display.
 * @return       Pointer to new label, or NULL on failure.
 */
label_t *label_create(widget_t *parent, const char *text);

/**
 * @brief Set the text displayed by a label.
 *
 * @param lbl  The label to modify.
 * @param text New text content (copied, max 255 chars).
 */
void label_set_text(label_t *lbl, const char *text);

/**
 * @brief Get the text displayed by a label.
 *
 * @param lbl The label to query.
 * @return    Pointer to label's text (do not free).
 */
const char *label_get_text(label_t *lbl);

/**
 * @brief Set the horizontal text alignment of a label.
 *
 * @param lbl   The label to modify.
 * @param align Alignment (ALIGN_LEFT, ALIGN_CENTER, or ALIGN_RIGHT).
 */
void label_set_alignment(label_t *lbl, alignment_t align);

/* ============================================================================
 * TextBox Functions
 * ============================================================================ */

/**
 * @brief Create a new text box widget.
 *
 * @param parent Parent widget to attach to, or NULL.
 * @return       Pointer to new textbox, or NULL on failure.
 */
textbox_t *textbox_create(widget_t *parent);

/**
 * @brief Set the text content of a text box.
 *
 * @param tb   The textbox to modify.
 * @param text New text content (copied).
 */
void textbox_set_text(textbox_t *tb, const char *text);

/**
 * @brief Get the text content of a text box.
 *
 * @param tb The textbox to query.
 * @return   Pointer to text content (do not free).
 */
const char *textbox_get_text(textbox_t *tb);

/**
 * @brief Enable or disable password mode.
 *
 * @details In password mode, all characters are displayed as asterisks.
 *
 * @param tb      The textbox to modify.
 * @param enabled True to show asterisks, false for normal display.
 */
void textbox_set_password_mode(textbox_t *tb, bool enabled);

/**
 * @brief Enable or disable multi-line mode (reserved).
 *
 * @param tb      The textbox to modify.
 * @param enabled True for multi-line, false for single-line.
 */
void textbox_set_multiline(textbox_t *tb, bool enabled);

/**
 * @brief Enable or disable read-only mode.
 *
 * @param tb       The textbox to modify.
 * @param readonly True to prevent editing, false to allow.
 */
void textbox_set_readonly(textbox_t *tb, bool readonly);

/**
 * @brief Set the callback for text changes.
 *
 * @param tb       The textbox to modify.
 * @param callback Function called when text content changes.
 * @param data     User data passed to callback.
 */
void textbox_set_onchange(textbox_t *tb, widget_callback_fn callback, void *data);

/**
 * @brief Set the callback for Enter key presses.
 *
 * @param tb       The textbox to modify.
 * @param callback Function called when Enter is pressed.
 * @param data     User data passed to callback.
 */
void textbox_set_onenter(textbox_t *tb, widget_callback_fn callback, void *data);

/**
 * @brief Get the current cursor position.
 *
 * @param tb The textbox to query.
 * @return   Cursor position (0 = before first character).
 */
int textbox_get_cursor_pos(textbox_t *tb);

/**
 * @brief Set the cursor position.
 *
 * @param tb  The textbox to modify.
 * @param pos New cursor position (clamped to valid range).
 */
void textbox_set_cursor_pos(textbox_t *tb, int pos);

/**
 * @brief Select all text in the textbox.
 *
 * @param tb The textbox to modify.
 */
void textbox_select_all(textbox_t *tb);

/**
 * @brief Clear any text selection.
 *
 * @param tb The textbox to modify.
 */
void textbox_clear_selection(textbox_t *tb);

/* ============================================================================
 * Checkbox Functions
 * ============================================================================ */

/**
 * @brief Create a new checkbox widget.
 *
 * @param parent Parent widget to attach to, or NULL.
 * @param text   Label text displayed next to checkbox.
 * @return       Pointer to new checkbox, or NULL on failure.
 */
checkbox_t *checkbox_create(widget_t *parent, const char *text);

/**
 * @brief Set the label text of a checkbox.
 *
 * @param cb   The checkbox to modify.
 * @param text New label text (copied, max 63 chars).
 */
void checkbox_set_text(checkbox_t *cb, const char *text);

/**
 * @brief Set the checked state of a checkbox.
 *
 * @param cb      The checkbox to modify.
 * @param checked True for checked, false for unchecked.
 */
void checkbox_set_checked(checkbox_t *cb, bool checked);

/**
 * @brief Get the checked state of a checkbox.
 *
 * @param cb The checkbox to query.
 * @return   True if checked, false if unchecked.
 */
bool checkbox_is_checked(checkbox_t *cb);

/**
 * @brief Set the callback for state changes.
 *
 * @param cb       The checkbox to modify.
 * @param callback Function called when checked state changes.
 * @param data     User data passed to callback.
 */
void checkbox_set_onchange(checkbox_t *cb, widget_callback_fn callback, void *data);

/* ============================================================================
 * ListView Functions
 * ============================================================================ */

/**
 * @brief Create a new listview widget.
 *
 * @param parent Parent widget to attach to, or NULL.
 * @return       Pointer to new listview, or NULL on failure.
 */
listview_t *listview_create(widget_t *parent);

/**
 * @brief Add an item to the end of a listview.
 *
 * @param lv   The listview to modify.
 * @param text Text content of the new item (copied).
 */
void listview_add_item(listview_t *lv, const char *text);

/**
 * @brief Insert an item at a specific position.
 *
 * @param lv    The listview to modify.
 * @param index Position to insert at (0 = beginning).
 * @param text  Text content of the new item (copied).
 */
void listview_insert_item(listview_t *lv, int index, const char *text);

/**
 * @brief Remove an item from a listview.
 *
 * @param lv    The listview to modify.
 * @param index Index of item to remove.
 */
void listview_remove_item(listview_t *lv, int index);

/**
 * @brief Remove all items from a listview.
 *
 * @param lv The listview to clear.
 */
void listview_clear(listview_t *lv);

/**
 * @brief Get the number of items in a listview.
 *
 * @param lv The listview to query.
 * @return   Number of items.
 */
int listview_get_count(listview_t *lv);

/**
 * @brief Get the text of an item.
 *
 * @param lv    The listview to query.
 * @param index Item index.
 * @return      Pointer to item text, or NULL if invalid index.
 */
const char *listview_get_item(listview_t *lv, int index);

/**
 * @brief Change the text of an existing item.
 *
 * @param lv    The listview to modify.
 * @param index Item index to modify.
 * @param text  New text content (copied).
 */
void listview_set_item(listview_t *lv, int index, const char *text);

/**
 * @brief Get the index of the selected item.
 *
 * @param lv The listview to query.
 * @return   Selected item index, or -1 if nothing selected.
 */
int listview_get_selected(listview_t *lv);

/**
 * @brief Set the selected item.
 *
 * @param lv    The listview to modify.
 * @param index Item index to select, or -1 to clear selection.
 */
void listview_set_selected(listview_t *lv, int index);

/**
 * @brief Set the callback for selection changes.
 *
 * @param lv       The listview to modify.
 * @param callback Function called when selection changes.
 * @param data     User data passed to callback.
 */
void listview_set_onselect(listview_t *lv, listview_select_fn callback, void *data);

/**
 * @brief Set the callback for double-clicks.
 *
 * @param lv       The listview to modify.
 * @param callback Function called when an item is double-clicked.
 * @param data     User data passed to callback.
 */
void listview_set_ondoubleclick(listview_t *lv, listview_select_fn callback, void *data);

/**
 * @brief Scroll the listview to make an item visible.
 *
 * @param lv    The listview to modify.
 * @param index Item index to ensure is visible.
 */
void listview_ensure_visible(listview_t *lv, int index);

/* ============================================================================
 * TreeView Functions
 * ============================================================================ */

/**
 * @brief Create a new treeview widget.
 *
 * @param parent Parent widget to attach to, or NULL.
 * @return       Pointer to new treeview, or NULL on failure.
 */
treeview_t *treeview_create(widget_t *parent);

/**
 * @brief Add a node to a treeview.
 *
 * @param tv     The treeview to modify.
 * @param parent Parent node, or NULL to add to root.
 * @param text   Text label for the new node.
 * @return       Pointer to new node, or NULL on failure.
 */
tree_node_t *treeview_add_node(treeview_t *tv, tree_node_t *parent, const char *text);

/**
 * @brief Remove a node and its children from a treeview.
 *
 * @param tv   The treeview to modify.
 * @param node Node to remove (and all descendants).
 */
void treeview_remove_node(treeview_t *tv, tree_node_t *node);

/**
 * @brief Remove all nodes from a treeview.
 *
 * @param tv The treeview to clear.
 */
void treeview_clear(treeview_t *tv);

/**
 * @brief Get the hidden root node of a treeview.
 *
 * @param tv The treeview to query.
 * @return   Pointer to root node (children are top-level items).
 */
tree_node_t *treeview_get_root(treeview_t *tv);

/**
 * @brief Get the currently selected node.
 *
 * @param tv The treeview to query.
 * @return   Selected node, or NULL if nothing selected.
 */
tree_node_t *treeview_get_selected(treeview_t *tv);

/**
 * @brief Set the selected node.
 *
 * @param tv   The treeview to modify.
 * @param node Node to select, or NULL to clear selection.
 */
void treeview_set_selected(treeview_t *tv, tree_node_t *node);

/**
 * @brief Expand a node to show its children.
 *
 * @param tv   The treeview containing the node.
 * @param node Node to expand.
 */
void treeview_expand(treeview_t *tv, tree_node_t *node);

/**
 * @brief Collapse a node to hide its children.
 *
 * @param tv   The treeview containing the node.
 * @param node Node to collapse.
 */
void treeview_collapse(treeview_t *tv, tree_node_t *node);

/**
 * @brief Toggle the expanded state of a node.
 *
 * @param tv   The treeview containing the node.
 * @param node Node to toggle.
 */
void treeview_toggle(treeview_t *tv, tree_node_t *node);

/**
 * @brief Set the callback for selection changes.
 *
 * @param tv       The treeview to modify.
 * @param callback Function called when selection changes.
 * @param data     User data passed to callback.
 */
void treeview_set_onselect(treeview_t *tv, treeview_select_fn callback, void *data);

/**
 * @brief Set the callback for expand/collapse events.
 *
 * @param tv       The treeview to modify.
 * @param callback Function called when a node is expanded or collapsed.
 * @param data     User data passed to callback.
 */
void treeview_set_onexpand(treeview_t *tv, treeview_select_fn callback, void *data);

/**
 * @brief Set the text of a tree node.
 *
 * @param node The node to modify.
 * @param text New text label (copied, max 63 chars).
 */
void tree_node_set_text(tree_node_t *node, const char *text);

/**
 * @brief Get the text of a tree node.
 *
 * @param node The node to query.
 * @return     Pointer to node's text (do not free).
 */
const char *tree_node_get_text(tree_node_t *node);

/**
 * @brief Associate user data with a tree node.
 *
 * @param node The node to modify.
 * @param data Application-defined data pointer.
 */
void tree_node_set_user_data(tree_node_t *node, void *data);

/**
 * @brief Get user data from a tree node.
 *
 * @param node The node to query.
 * @return     Previously set user data, or NULL.
 */
void *tree_node_get_user_data(tree_node_t *node);

/**
 * @brief Get the number of children of a tree node.
 *
 * @param node The node to query.
 * @return     Number of child nodes.
 */
int tree_node_get_child_count(tree_node_t *node);

/**
 * @brief Get a child node by index.
 *
 * @param node  The parent node.
 * @param index Zero-based child index.
 * @return      Child node, or NULL if index out of range.
 */
tree_node_t *tree_node_get_child(tree_node_t *node, int index);

/**
 * @brief Get the parent of a tree node.
 *
 * @param node The node to query.
 * @return     Parent node, or NULL for root-level nodes.
 */
tree_node_t *tree_node_get_parent(tree_node_t *node);

/* ============================================================================
 * Menu Functions
 * ============================================================================ */

/**
 * @brief Create a new popup menu.
 *
 * @return Pointer to new menu, or NULL on failure.
 */
menu_t *menu_create(void);

/**
 * @brief Destroy a menu and free its resources.
 *
 * @param m The menu to destroy. May be NULL (no-op).
 */
void menu_destroy(menu_t *m);

/**
 * @brief Add a clickable item to a menu.
 *
 * @param m        The menu to modify.
 * @param text     Item label text.
 * @param callback Function called when item is clicked.
 * @param data     User data passed to callback.
 */
void menu_add_item(menu_t *m, const char *text, widget_callback_fn callback, void *data);

/**
 * @brief Add an item with a keyboard shortcut hint.
 *
 * @param m        The menu to modify.
 * @param text     Item label text.
 * @param shortcut Shortcut text (e.g., "Ctrl+S"), displayed right-aligned.
 * @param callback Function called when item is clicked.
 * @param data     User data passed to callback.
 */
void menu_add_item_with_shortcut(menu_t *m, const char *text, const char *shortcut,
                                  widget_callback_fn callback, void *data);

/**
 * @brief Add a separator line to a menu.
 *
 * @param m The menu to modify.
 */
void menu_add_separator(menu_t *m);

/**
 * @brief Add a submenu item.
 *
 * @param m       The parent menu.
 * @param text    Label for the submenu item.
 * @param submenu The submenu to show when this item is hovered.
 */
void menu_add_submenu(menu_t *m, const char *text, menu_t *submenu);

/**
 * @brief Enable or disable a menu item.
 *
 * @param m       The menu to modify.
 * @param index   Item index.
 * @param enabled True to enable, false to disable.
 */
void menu_set_item_enabled(menu_t *m, int index, bool enabled);

/**
 * @brief Set the checked state of a menu item.
 *
 * @param m       The menu to modify.
 * @param index   Item index.
 * @param checked True to show checkmark, false to hide.
 */
void menu_set_item_checked(menu_t *m, int index, bool checked);

/**
 * @brief Display a menu at a specified position.
 *
 * @param m   The menu to show.
 * @param win The window to display the menu in.
 * @param x   X coordinate for menu's top-left corner.
 * @param y   Y coordinate for menu's top-left corner.
 */
void menu_show(menu_t *m, gui_window_t *win, int x, int y);

/**
 * @brief Hide a menu.
 *
 * @param m The menu to hide.
 */
void menu_hide(menu_t *m);

/**
 * @brief Check if a menu is currently visible.
 *
 * @param m The menu to query.
 * @return  True if visible, false if hidden.
 */
bool menu_is_visible(menu_t *m);

/**
 * @brief Handle a mouse event for a menu.
 *
 * @param m          The menu.
 * @param x          X coordinate.
 * @param y          Y coordinate.
 * @param button     Mouse button.
 * @param event_type Event type (0=move, 1=press, 2=release).
 * @return           True if event was handled.
 */
bool menu_handle_mouse(menu_t *m, int x, int y, int button, int event_type);

/**
 * @brief Paint a menu to a window.
 *
 * @param m   The menu to paint.
 * @param win The window to paint into.
 */
void menu_paint(menu_t *m, gui_window_t *win);

/* ============================================================================
 * ProgressBar Functions
 * ============================================================================ */

/**
 * @brief Create a new progress bar widget.
 *
 * @param parent Parent widget to attach to, or NULL.
 * @return       Pointer to new progressbar, or NULL on failure.
 */
progressbar_t *progressbar_create(widget_t *parent);

/**
 * @brief Set the current progress value.
 *
 * @param pb    The progressbar to modify.
 * @param value New value (clamped to min/max range).
 */
void progressbar_set_value(progressbar_t *pb, int value);

/**
 * @brief Get the current progress value.
 *
 * @param pb The progressbar to query.
 * @return   Current value.
 */
int progressbar_get_value(progressbar_t *pb);

/**
 * @brief Set the value range for a progress bar.
 *
 * @param pb      The progressbar to modify.
 * @param min_val Minimum value (left edge = empty).
 * @param max_val Maximum value (right edge = full).
 */
void progressbar_set_range(progressbar_t *pb, int min_val, int max_val);

/**
 * @brief Enable or disable percentage text display.
 *
 * @param pb   The progressbar to modify.
 * @param show True to show "XX%" text, false to hide.
 */
void progressbar_set_show_text(progressbar_t *pb, bool show);

/* ============================================================================
 * Scrollbar Functions
 * ============================================================================ */

/**
 * @brief Create a new scrollbar widget.
 *
 * @param parent   Parent widget to attach to, or NULL.
 * @param vertical True for vertical, false for horizontal.
 * @return         Pointer to new scrollbar, or NULL on failure.
 */
scrollbar_t *scrollbar_create(widget_t *parent, bool vertical);

/**
 * @brief Set the scroll position.
 *
 * @param sb    The scrollbar to modify.
 * @param value New position (clamped to range).
 */
void scrollbar_set_value(scrollbar_t *sb, int value);

/**
 * @brief Get the current scroll position.
 *
 * @param sb The scrollbar to query.
 * @return   Current position.
 */
int scrollbar_get_value(scrollbar_t *sb);

/**
 * @brief Set the scroll range.
 *
 * @param sb      The scrollbar to modify.
 * @param min_val Minimum position.
 * @param max_val Maximum position.
 */
void scrollbar_set_range(scrollbar_t *sb, int min_val, int max_val);

/**
 * @brief Set the page size (visible portion).
 *
 * @details
 * The page size affects the thumb size proportionally and is used
 * when clicking in the track to page up/down.
 *
 * @param sb        The scrollbar to modify.
 * @param page_size Size of the visible portion.
 */
void scrollbar_set_page_size(scrollbar_t *sb, int page_size);

/**
 * @brief Set the callback for position changes.
 *
 * @param sb       The scrollbar to modify.
 * @param callback Function called when position changes.
 * @param data     User data passed to callback.
 */
void scrollbar_set_onchange(scrollbar_t *sb, widget_callback_fn callback, void *data);

/* ============================================================================
 * Layout Functions
 * ============================================================================ */

/**
 * @brief Create a new layout manager.
 *
 * @param type Layout algorithm type.
 * @return     Pointer to new layout, or NULL on failure.
 */
layout_t *layout_create(layout_type_t type);

/**
 * @brief Destroy a layout manager.
 *
 * @param layout The layout to destroy. May be NULL (no-op).
 */
void layout_destroy(layout_t *layout);

/**
 * @brief Set the spacing between child widgets.
 *
 * @param layout  The layout to modify.
 * @param spacing Space in pixels between adjacent children.
 */
void layout_set_spacing(layout_t *layout, int spacing);

/**
 * @brief Set the margins around the layout area.
 *
 * @param layout The layout to modify.
 * @param left   Left margin in pixels.
 * @param top    Top margin in pixels.
 * @param right  Right margin in pixels.
 * @param bottom Bottom margin in pixels.
 */
void layout_set_margins(layout_t *layout, int left, int top, int right, int bottom);

/**
 * @brief Configure grid dimensions for LAYOUT_GRID.
 *
 * @param layout  The layout to modify.
 * @param columns Number of columns.
 * @param rows    Number of rows.
 */
void layout_set_grid(layout_t *layout, int columns, int rows);

/**
 * @brief Attach a layout manager to a container widget.
 *
 * @param container The container widget.
 * @param layout    The layout manager (ownership transferred).
 */
void widget_set_layout(widget_t *container, layout_t *layout);

/**
 * @brief Set the layout constraint for a widget (for LAYOUT_BORDER).
 *
 * @param w          The widget to modify.
 * @param constraint Border region (BORDER_NORTH, etc.).
 */
void widget_set_layout_constraint(widget_t *w, int constraint);

/**
 * @brief Apply the layout to position all children.
 *
 * @details
 * Calculates and sets the position and size of all child widgets
 * according to the layout algorithm. Call this after adding/removing
 * children or changing the container's size.
 *
 * @param container The container widget with a layout attached.
 */
void layout_apply(widget_t *container);

/* ============================================================================
 * Dialog Functions
 * ============================================================================ */

/**
 * @brief Display a modal message box dialog.
 *
 * @details
 * Shows a modal dialog with a message and configurable buttons.
 * Blocks until the user clicks a button or closes the dialog.
 *
 * @param parent  Parent window (for positioning), or NULL.
 * @param title   Dialog title bar text.
 * @param message Message text to display.
 * @param type    Button configuration (OK, OK/Cancel, etc.).
 * @param icon    Icon to display (info, warning, error, question).
 * @return        Which button was clicked (MB_RESULT_OK, etc.).
 */
msgbox_result_t msgbox_show(gui_window_t *parent, const char *title, const char *message,
                            msgbox_type_t type, msgbox_icon_t icon);

/**
 * @brief Display a file open dialog.
 *
 * @param parent      Parent window (for positioning), or NULL.
 * @param title       Dialog title bar text.
 * @param filter      File filter pattern (e.g., "*.txt"), or NULL for all.
 * @param initial_dir Starting directory, or NULL for current.
 * @return            Selected file path (caller must free), or NULL if canceled.
 */
char *filedialog_open(gui_window_t *parent, const char *title, const char *filter,
                      const char *initial_dir);

/**
 * @brief Display a file save dialog.
 *
 * @param parent      Parent window (for positioning), or NULL.
 * @param title       Dialog title bar text.
 * @param filter      File filter pattern (e.g., "*.txt"), or NULL for all.
 * @param initial_dir Starting directory, or NULL for current.
 * @return            Selected file path (caller must free), or NULL if canceled.
 */
char *filedialog_save(gui_window_t *parent, const char *title, const char *filter,
                      const char *initial_dir);

/**
 * @brief Display a folder selection dialog.
 *
 * @param parent      Parent window (for positioning), or NULL.
 * @param title       Dialog title bar text.
 * @param initial_dir Starting directory, or NULL for current.
 * @return            Selected folder path (caller must free), or NULL if canceled.
 */
char *filedialog_folder(gui_window_t *parent, const char *title, const char *initial_dir);

/* ============================================================================
 * 3D Drawing Functions (Amiga-style)
 * ============================================================================ */

/**
 * @brief Draw a raised (outward) 3D border.
 *
 * @details
 * Draws a rectangle with beveled edges that appears to protrude from
 * the surface. Light edges on top/left, dark edges on bottom/right.
 * Used for buttons in unpressed state.
 *
 * @param win    Window to draw into.
 * @param x      Left edge X coordinate.
 * @param y      Top edge Y coordinate.
 * @param w      Width in pixels.
 * @param h      Height in pixels.
 * @param face   Face (background) color.
 * @param light  Highlight color for top/left edges.
 * @param shadow Shadow color for bottom/right edges.
 */
void draw_3d_raised(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow);

/**
 * @brief Draw a sunken (inward) 3D border.
 *
 * @details
 * Draws a rectangle with beveled edges that appears to be pressed into
 * the surface. Dark edges on top/left, light edges on bottom/right.
 * Used for input fields and pressed buttons.
 *
 * @param win    Window to draw into.
 * @param x      Left edge X coordinate.
 * @param y      Top edge Y coordinate.
 * @param w      Width in pixels.
 * @param h      Height in pixels.
 * @param face   Face (background) color.
 * @param light  Highlight color for bottom/right edges.
 * @param shadow Shadow color for top/left edges.
 */
void draw_3d_sunken(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow);

/**
 * @brief Draw a standard 3D button.
 *
 * @details
 * Convenience function that draws a button with default Workbench colors.
 * The pressed parameter controls whether the button appears raised or sunken.
 *
 * @param win     Window to draw into.
 * @param x       Left edge X coordinate.
 * @param y       Top edge Y coordinate.
 * @param w       Width in pixels.
 * @param h       Height in pixels.
 * @param pressed True for sunken (pressed) appearance, false for raised.
 */
void draw_3d_button(gui_window_t *win, int x, int y, int w, int h, bool pressed);

/**
 * @brief Draw a 3D frame border.
 *
 * @details
 * Draws a decorative frame border around an area, commonly used to
 * group related controls.
 *
 * @param win    Window to draw into.
 * @param x      Left edge X coordinate.
 * @param y      Top edge Y coordinate.
 * @param w      Width in pixels.
 * @param h      Height in pixels.
 * @param sunken True for inward frame, false for outward.
 */
void draw_3d_frame(gui_window_t *win, int x, int y, int w, int h, bool sunken);

/**
 * @brief Draw a 3D groove (etched line).
 *
 * @details
 * Draws an etched horizontal line used as a separator or divider.
 * Consists of a dark line above a light line to create depth effect.
 *
 * @param win Window to draw into.
 * @param x   Left edge X coordinate.
 * @param y   Y coordinate of groove.
 * @param w   Width in pixels.
 * @param h   Height in pixels (typically 2).
 */
void draw_3d_groove(gui_window_t *win, int x, int y, int w, int h);

/* ============================================================================
 * Widget Application Helper
 * ============================================================================ */

/**
 * @brief Application state for widget-based programs.
 *
 * @details
 * This structure encapsulates the state needed to run a widget-based
 * application, including the window, root widget hierarchy, focus
 * tracking, and run loop control.
 *
 * Use widget_app_create() to initialize, widget_app_run() to enter
 * the event loop, and widget_app_quit() to exit.
 */
typedef struct {
    gui_window_t *window;   /**< The application's main window */
    widget_t *root;         /**< Root widget of the UI hierarchy */
    widget_t *focused;      /**< Currently focused widget */
    menu_t *active_menu;    /**< Currently visible menu (if any) */
    bool running;           /**< False to exit the run loop */
} widget_app_t;

/**
 * @brief Create a new widget application.
 *
 * @details
 * Initializes the GUI system, creates a window, and sets up the
 * application state for running a widget-based UI.
 *
 * @param title  Window title bar text.
 * @param width  Window width in pixels.
 * @param height Window height in pixels.
 * @return       Pointer to application state, or NULL on failure.
 */
widget_app_t *widget_app_create(const char *title, int width, int height);

/**
 * @brief Destroy a widget application and free resources.
 *
 * @details
 * Destroys the root widget hierarchy, closes the window, shuts down
 * the GUI system, and frees the application state.
 *
 * @param app The application to destroy.
 */
void widget_app_destroy(widget_app_t *app);

/**
 * @brief Set the root widget for an application.
 *
 * @param app  The application.
 * @param root Root widget of the UI hierarchy.
 */
void widget_app_set_root(widget_app_t *app, widget_t *root);

/**
 * @brief Run the application event loop.
 *
 * @details
 * Enters a blocking event loop that:
 * 1. Polls for GUI events (mouse, keyboard)
 * 2. Dispatches events to widgets
 * 3. Repaints the UI as needed
 * 4. Yields to other processes
 *
 * Returns when widget_app_quit() is called or the window is closed.
 *
 * @param app The application to run.
 */
void widget_app_run(widget_app_t *app);

/**
 * @brief Request that the application event loop exit.
 *
 * @param app The application.
 */
void widget_app_quit(widget_app_t *app);

/**
 * @brief Request a repaint of the application window.
 *
 * @param app The application.
 */
void widget_app_repaint(widget_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_WIDGET_H */
