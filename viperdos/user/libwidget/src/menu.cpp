//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file menu.c
 * @brief Popup and dropdown menu system implementation for the libwidget toolkit.
 *
 * This file implements a menu system that provides popup menus for context menus,
 * dropdown menus for menu bars, and hierarchical submenus. Menus are fundamental
 * UI elements used for:
 * - Application menu bars (File, Edit, View, etc.)
 * - Context menus (right-click menus)
 * - Option selection popups
 * - Command access with keyboard shortcuts
 *
 * ## Menu Structure
 *
 * A menu consists of:
 * - **Items**: Clickable entries with text, optional shortcut hint, and callback
 * - **Separators**: Visual dividers between groups of related items
 * - **Submenus**: Nested menus that appear when hovering over an item
 *
 * ## Visual Design
 *
 * Menus are rendered as raised 3D panels with:
 * - Light gray background (WB_GRAY_LIGHT)
 * - Raised 3D border for a "floating" appearance
 * - Blue highlight for the currently hovered item
 * - Separator lines with 3D groove effect
 * - Right arrow indicator for items with submenus
 * - Keyboard shortcut hints right-aligned
 *
 * ## Menu Item Layout
 *
 * Each menu item consists of (left to right):
 * 1. Checkmark area (16 pixels) - shows "*" if item is checked
 * 2. Item text - the main label
 * 3. Gap (20 pixels) - space between text and shortcut
 * 4. Shortcut hint - right-aligned (e.g., "Ctrl+S")
 * 5. Submenu arrow - ">" for items with submenus
 *
 * ## Size Calculation
 *
 * The menu width is calculated based on the widest item text plus the widest
 * shortcut hint, with a minimum width of 100 pixels. Height is the sum of all
 * item heights (20 pixels for normal items, 8 pixels for separators) plus
 * vertical padding.
 *
 * @see widget.h for the menu_t and menu_item_t structure definitions
 */
//===----------------------------------------------------------------------===//

#include <stdlib.h>
#include <string.h>
#include <widget.h>

/**
 * @brief Height of a normal (non-separator) menu item in pixels.
 *
 * Each menu item occupies 20 pixels of vertical space, providing room for
 * text (10 pixels) plus 5 pixels padding above and below.
 */
#define MENU_ITEM_HEIGHT 20

/**
 * @brief Vertical padding at the top and bottom of the menu.
 *
 * The menu has 4 pixels of padding before the first item and after the last.
 */
#define MENU_PADDING 4

/**
 * @brief Minimum width of a menu in pixels.
 *
 * Even menus with very short items will be at least 100 pixels wide to
 * provide a reasonable click target.
 */
#define MENU_MIN_WIDTH 100

/**
 * @brief Height of a separator item in pixels.
 *
 * Separators are shorter than normal items at 8 pixels, just enough for
 * the 3D groove line plus some vertical spacing.
 */
#define SEPARATOR_HEIGHT 8

/**
 * @brief Initial capacity for the items array.
 *
 * Menus start with space for 8 items. When this capacity is exceeded,
 * the array doubles in size.
 */
#define INITIAL_CAPACITY 8

//===----------------------------------------------------------------------===//
// Menu API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new empty menu.
 *
 * This function allocates and initializes a menu with no items. Items can be
 * added using menu_add_item(), menu_add_separator(), and menu_add_submenu().
 *
 * The menu starts hidden and must be shown with menu_show() to become visible.
 *
 * @return Pointer to the newly created menu, or NULL if memory allocation failed.
 *
 * @code
 * // Create a file menu
 * menu_t *file_menu = menu_create();
 * menu_add_item_with_shortcut(file_menu, "New", "Ctrl+N", on_new, NULL);
 * menu_add_item_with_shortcut(file_menu, "Open", "Ctrl+O", on_open, NULL);
 * menu_add_separator(file_menu);
 * menu_add_item_with_shortcut(file_menu, "Save", "Ctrl+S", on_save, NULL);
 * menu_add_separator(file_menu);
 * menu_add_item(file_menu, "Exit", on_exit, NULL);
 * @endcode
 *
 * @see menu_destroy() To free the menu when done
 * @see menu_show() To display the menu
 */
menu_t *menu_create(void) {
    menu_t *m = (menu_t *)malloc(sizeof(menu_t));
    if (!m)
        return NULL;

    memset(m, 0, sizeof(menu_t));

    m->item_capacity = INITIAL_CAPACITY;
    m->items = (menu_item_t *)malloc(m->item_capacity * sizeof(menu_item_t));
    if (!m->items) {
        free(m);
        return NULL;
    }

    m->hovered_index = -1;
    return m;
}

/**
 * @brief Destroys a menu and frees all associated memory.
 *
 * This function recursively destroys any submenus attached to menu items,
 * then frees the items array and the menu structure itself.
 *
 * @param m Pointer to the menu to destroy. If NULL, does nothing.
 *
 * @warning After calling this function, the menu pointer becomes invalid.
 *          Do not use it after destruction.
 *
 * @see menu_create() To create a new menu
 */
void menu_destroy(menu_t *m) {
    if (!m)
        return;

    // Destroy submenus
    for (int i = 0; i < m->item_count; i++) {
        if (m->items[i].submenu) {
            menu_destroy(m->items[i].submenu);
        }
    }

    free(m->items);
    free(m);
}

/**
 * @brief Grows the items array if it's full.
 *
 * This internal helper function doubles the capacity of the items array
 * when the current capacity is reached. It uses realloc to preserve
 * existing items.
 *
 * @param m Pointer to the menu. Must not be NULL.
 *
 * @note This function is called internally by the menu_add_* functions.
 *       If reallocation fails, the menu remains unchanged.
 */
static void menu_grow_if_needed(menu_t *m) {
    if (m->item_count >= m->item_capacity) {
        int new_cap = m->item_capacity * 2;
        menu_item_t *new_items = (menu_item_t *)realloc(m->items, new_cap * sizeof(menu_item_t));
        if (!new_items)
            return;
        m->items = new_items;
        m->item_capacity = new_cap;
    }
}

/**
 * @brief Adds a menu item with a text label and callback.
 *
 * This is a convenience wrapper around menu_add_item_with_shortcut() that
 * adds an item without a keyboard shortcut hint.
 *
 * @param m        Pointer to the menu. If NULL, does nothing.
 * @param text     The display text for the menu item.
 * @param callback The function to call when the item is clicked.
 * @param data     User-defined data passed to the callback.
 *
 * @see menu_add_item_with_shortcut() To include a shortcut hint
 * @see menu_add_separator() To add a visual separator
 */
void menu_add_item(menu_t *m, const char *text, widget_callback_fn callback, void *data) {
    menu_add_item_with_shortcut(m, text, NULL, callback, data);
}

/**
 * @brief Adds a menu item with text, keyboard shortcut hint, and callback.
 *
 * This function adds a new clickable item to the menu. When the user clicks
 * on the item, the callback function is invoked.
 *
 * @param m        Pointer to the menu. If NULL, does nothing.
 * @param text     The display text for the menu item. Copied into a 64-byte
 *                 internal buffer (maximum 63 characters).
 * @param shortcut Optional keyboard shortcut hint (e.g., "Ctrl+S"). This is
 *                 purely visual—it does not register actual keyboard handling.
 *                 Pass NULL for no shortcut hint.
 * @param callback The function to call when the item is clicked, or NULL for
 *                 no action. Signature: void (*)(void*)
 * @param data     User-defined data passed to the callback function.
 *
 * @note The item is enabled by default. Use menu_set_item_enabled() to disable.
 *
 * @code
 * // Add a save item with Ctrl+S hint
 * menu_add_item_with_shortcut(file_menu, "Save", "Ctrl+S", on_save, doc);
 *
 * // Add an item without shortcut
 * menu_add_item_with_shortcut(file_menu, "Properties", NULL, on_props, doc);
 * @endcode
 *
 * @see menu_add_item() Convenience wrapper without shortcut
 * @see menu_set_item_enabled() To enable/disable the item
 */
void menu_add_item_with_shortcut(
    menu_t *m, const char *text, const char *shortcut, widget_callback_fn callback, void *data) {
    if (!m)
        return;

    menu_grow_if_needed(m);

    menu_item_t *item = &m->items[m->item_count++];
    memset(item, 0, sizeof(menu_item_t));

    if (text) {
        strncpy(item->text, text, sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    }

    if (shortcut) {
        strncpy(item->shortcut, shortcut, sizeof(item->shortcut) - 1);
        item->shortcut[sizeof(item->shortcut) - 1] = '\0';
    }

    item->enabled = true;
    item->on_click = callback;
    item->callback_data = data;
}

/**
 * @brief Adds a separator line between menu items.
 *
 * Separators are visual dividers used to group related menu items together.
 * They are rendered as horizontal 3D groove lines and cannot be clicked
 * or hovered.
 *
 * @param m Pointer to the menu. If NULL, does nothing.
 *
 * @code
 * menu_add_item(m, "Cut", on_cut, NULL);
 * menu_add_item(m, "Copy", on_copy, NULL);
 * menu_add_item(m, "Paste", on_paste, NULL);
 * menu_add_separator(m);  // Divider before delete
 * menu_add_item(m, "Delete", on_delete, NULL);
 * @endcode
 *
 * @see menu_add_item() To add clickable items
 */
void menu_add_separator(menu_t *m) {
    if (!m)
        return;

    menu_grow_if_needed(m);

    menu_item_t *item = &m->items[m->item_count++];
    memset(item, 0, sizeof(menu_item_t));
    item->separator = true;
    item->enabled = false;
}

/**
 * @brief Adds a submenu item that opens another menu when hovered.
 *
 * This function creates a menu item that, when clicked or hovered, displays
 * a child menu to its right. The submenu arrow indicator (">") is automatically
 * shown for items with submenus.
 *
 * @param m       Pointer to the parent menu. If NULL, does nothing.
 * @param text    The display text for the submenu item.
 * @param submenu Pointer to the child menu to display. Ownership is transferred
 *                to the parent menu—the submenu will be destroyed when the
 *                parent menu is destroyed.
 *
 * @code
 * // Create a "Recent Files" submenu
 * menu_t *recent = menu_create();
 * menu_add_item(recent, "Document1.txt", on_open_recent, "doc1.txt");
 * menu_add_item(recent, "Document2.txt", on_open_recent, "doc2.txt");
 *
 * // Add to File menu
 * menu_add_submenu(file_menu, "Recent Files", recent);
 * @endcode
 *
 * @warning Do not destroy the submenu manually after passing it to this
 *          function. The parent menu takes ownership and will destroy it.
 *
 * @see menu_create() To create the submenu
 */
void menu_add_submenu(menu_t *m, const char *text, menu_t *submenu) {
    if (!m)
        return;

    menu_grow_if_needed(m);

    menu_item_t *item = &m->items[m->item_count++];
    memset(item, 0, sizeof(menu_item_t));

    if (text) {
        strncpy(item->text, text, sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    }

    item->enabled = true;
    item->submenu = submenu;
}

/**
 * @brief Enables or disables a menu item.
 *
 * Disabled items are rendered in gray text and cannot be clicked or hovered.
 * They remain visible but non-interactive.
 *
 * @param m       Pointer to the menu. If NULL, does nothing.
 * @param index   The zero-based index of the item to modify.
 * @param enabled True to enable the item, false to disable it.
 *
 * @code
 * // Disable "Save" when there are no changes
 * if (!document_modified) {
 *     menu_set_item_enabled(file_menu, save_index, false);
 * }
 * @endcode
 *
 * @see menu_add_item() Items are enabled by default when added
 */
void menu_set_item_enabled(menu_t *m, int index, bool enabled) {
    if (!m || index < 0 || index >= m->item_count)
        return;
    m->items[index].enabled = enabled;
}

/**
 * @brief Sets or clears the checkmark on a menu item.
 *
 * Checked items display a checkmark symbol ("*") in the left margin before
 * the item text. This is used for toggle-style options like "Show Toolbar"
 * or exclusive selections in a group.
 *
 * @param m       Pointer to the menu. If NULL, does nothing.
 * @param index   The zero-based index of the item to modify.
 * @param checked True to show a checkmark, false to remove it.
 *
 * @code
 * // Toggle toolbar visibility
 * static void on_toggle_toolbar(void *data) {
 *     show_toolbar = !show_toolbar;
 *     menu_set_item_checked(view_menu, toolbar_index, show_toolbar);
 * }
 * @endcode
 *
 * @note The checkmark does not affect the item's behavior. The application
 *       is responsible for tracking the actual state and updating the check.
 */
void menu_set_item_checked(menu_t *m, int index, bool checked) {
    if (!m || index < 0 || index >= m->item_count)
        return;
    m->items[index].checked = checked;
}

/**
 * @brief Calculates the optimal width and height for the menu.
 *
 * This internal function measures all items to determine the menu dimensions:
 * - **Width**: Maximum text width + maximum shortcut width + padding + gaps.
 *   Minimum 100 pixels.
 * - **Height**: Sum of all item heights (20px for normal, 8px for separators)
 *   plus top and bottom padding.
 *
 * @param m Pointer to the menu. Must not be NULL.
 *
 * @note This function updates m->width and m->height fields.
 */
static void menu_calculate_size(menu_t *m) {
    int max_text_width = 0;
    int max_shortcut_width = 0;

    for (int i = 0; i < m->item_count; i++) {
        menu_item_t *item = &m->items[i];

        if (!item->separator) {
            int text_width = (int)strlen(item->text) * 8;
            if (text_width > max_text_width) {
                max_text_width = text_width;
            }

            if (item->shortcut[0]) {
                int shortcut_width = (int)strlen(item->shortcut) * 8;
                if (shortcut_width > max_shortcut_width) {
                    max_shortcut_width = shortcut_width;
                }
            }
        }
    }

    m->width = max_text_width + max_shortcut_width + MENU_PADDING * 4;
    if (max_shortcut_width > 0) {
        m->width += 20; // Gap between text and shortcut
    }
    if (m->width < MENU_MIN_WIDTH) {
        m->width = MENU_MIN_WIDTH;
    }

    m->height = MENU_PADDING * 2;
    for (int i = 0; i < m->item_count; i++) {
        m->height += m->items[i].separator ? SEPARATOR_HEIGHT : MENU_ITEM_HEIGHT;
    }
}

/**
 * @brief Displays the menu at the specified screen position.
 *
 * This function makes the menu visible and positions it at the given (x, y)
 * coordinates. The menu size is calculated based on its contents before
 * displaying.
 *
 * @param m   Pointer to the menu. If NULL, does nothing.
 * @param win The GUI window context (currently unused, may be NULL).
 * @param x   The X coordinate for the menu's top-left corner.
 * @param y   The Y coordinate for the menu's top-left corner.
 *
 * @note The coordinates are in screen space, not widget-local space.
 *
 * @note If the menu would extend off the screen, the caller is responsible
 *       for adjusting the position (this function does not perform bounds
 *       checking).
 *
 * @code
 * // Show context menu at mouse position
 * menu_show(context_menu, win, mouse_x, mouse_y);
 *
 * // Show dropdown below a button
 * menu_show(dropdown, win, button_x, button_y + button_height);
 * @endcode
 *
 * @see menu_hide() To hide the menu
 * @see menu_is_visible() To check if the menu is visible
 */
void menu_show(menu_t *m, gui_window_t *win, int x, int y) {
    (void)win;
    if (!m)
        return;

    menu_calculate_size(m);

    m->x = x;
    m->y = y;
    m->visible = true;
    m->hovered_index = -1;
}

/**
 * @brief Hides the menu and any visible submenus.
 *
 * This function makes the menu invisible and recursively hides all its
 * submenus. The hover state is also reset.
 *
 * @param m Pointer to the menu. If NULL, does nothing.
 *
 * @see menu_show() To display the menu
 * @see menu_is_visible() To check visibility
 */
void menu_hide(menu_t *m) {
    if (m) {
        m->visible = false;
        m->hovered_index = -1;

        // Hide submenus
        for (int i = 0; i < m->item_count; i++) {
            if (m->items[i].submenu) {
                menu_hide(m->items[i].submenu);
            }
        }
    }
}

/**
 * @brief Checks whether the menu is currently visible.
 *
 * @param m Pointer to the menu. If NULL, returns false.
 *
 * @return True if the menu is visible, false otherwise.
 *
 * @see menu_show() To make the menu visible
 * @see menu_hide() To hide the menu
 */
bool menu_is_visible(menu_t *m) {
    return m ? m->visible : false;
}

/**
 * @brief Processes mouse events for the menu.
 *
 * This function handles mouse movement and clicks within the menu area.
 * It updates the hover state as the mouse moves and executes item callbacks
 * when items are clicked.
 *
 * ## Event Handling
 *
 * - **Mouse move**: Updates which item is hovered (shown with blue highlight)
 * - **Left click on item**: Executes the item's callback and hides the menu
 * - **Left click on submenu item**: Opens the submenu to the right
 * - **Left click outside menu**: Hides the menu
 *
 * @param m          Pointer to the menu. If NULL or not visible, returns false.
 * @param x          X coordinate of the mouse in screen space.
 * @param y          Y coordinate of the mouse in screen space.
 * @param button     Mouse button identifier (0 = left, 1 = right, 2 = middle).
 * @param event_type The type of mouse event (1 = button down).
 *
 * @return True if the event was handled by this menu (mouse was inside),
 *         false if the event should be processed by other elements.
 *
 * @note Disabled items and separators do not respond to clicks or hover.
 *
 * @note Submenus are automatically shown when clicking on a submenu item.
 */
bool menu_handle_mouse(menu_t *m, int x, int y, int button, int event_type) {
    if (!m || !m->visible)
        return false;

    // Check if inside menu
    bool inside = (x >= m->x && x < m->x + m->width && y >= m->y && y < m->y + m->height);

    if (!inside) {
        if (event_type == 1 && button == 0) { // Click outside
            menu_hide(m);
        }
        return false;
    }

    // Find hovered item
    int item_y = m->y + MENU_PADDING;
    int hovered = -1;

    for (int i = 0; i < m->item_count; i++) {
        menu_item_t *item = &m->items[i];
        int item_height = item->separator ? SEPARATOR_HEIGHT : MENU_ITEM_HEIGHT;

        if (y >= item_y && y < item_y + item_height) {
            if (!item->separator && item->enabled) {
                hovered = i;
            }
            break;
        }

        item_y += item_height;
    }

    m->hovered_index = hovered;

    // Handle click
    if (event_type == 1 && button == 0 && hovered >= 0) {
        menu_item_t *item = &m->items[hovered];

        if (item->submenu) {
            // Show submenu
            menu_show(item->submenu, NULL, m->x + m->width - 4, item_y);
        } else if (item->on_click) {
            // Execute callback
            item->on_click(item->callback_data);
            menu_hide(m);
        }

        return true;
    }

    return true;
}

/**
 * @brief Renders the menu and any visible submenus.
 *
 * This function draws the complete menu visual representation:
 *
 * 1. **Background and Border**: A raised 3D panel with light gray background
 * 2. **Menu Items**: Each item drawn with:
 *    - Blue highlight if hovered
 *    - Checkmark (*) if checked
 *    - Gray text if disabled, black if enabled, white if hovered
 *    - Right-aligned shortcut hint
 *    - Right arrow (>) for submenu items
 * 3. **Separators**: 3D groove lines between item groups
 * 4. **Submenus**: Recursively painted if visible
 *
 * @param m   Pointer to the menu. If NULL or not visible, does nothing.
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note This function should be called during the paint phase of the
 *       application's event loop, after painting other UI elements so
 *       the menu appears on top.
 *
 * @see menu_show() Which sets the visible flag
 * @see menu_handle_mouse() Which updates the hover state
 */
void menu_paint(menu_t *m, gui_window_t *win) {
    if (!m || !m->visible)
        return;

    int x = m->x;
    int y = m->y;

    // Draw menu background with 3D border
    draw_3d_raised(win, x, y, m->width, m->height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

    // Draw items
    int item_y = y + MENU_PADDING;

    for (int i = 0; i < m->item_count; i++) {
        menu_item_t *item = &m->items[i];

        if (item->separator) {
            // Draw separator line
            int sep_y = item_y + SEPARATOR_HEIGHT / 2;
            gui_draw_hline(win, x + MENU_PADDING, x + m->width - MENU_PADDING, sep_y, WB_GRAY_DARK);
            gui_draw_hline(win, x + MENU_PADDING, x + m->width - MENU_PADDING, sep_y + 1, WB_WHITE);
            item_y += SEPARATOR_HEIGHT;
        } else {
            // Highlight hovered item
            if (i == m->hovered_index) {
                gui_fill_rect(win, x + 2, item_y, m->width - 4, MENU_ITEM_HEIGHT, WB_BLUE);
            }

            // Draw checkmark if checked
            if (item->checked) {
                uint32_t check_color = (i == m->hovered_index) ? WB_WHITE : WB_BLACK;
                gui_draw_text(win, x + MENU_PADDING, item_y + 5, "*", check_color);
            }

            // Draw text
            uint32_t text_color;
            if (!item->enabled) {
                text_color = WB_GRAY_MED;
            } else if (i == m->hovered_index) {
                text_color = WB_WHITE;
            } else {
                text_color = WB_BLACK;
            }

            gui_draw_text(win, x + MENU_PADDING + 16, item_y + 5, item->text, text_color);

            // Draw shortcut
            if (item->shortcut[0]) {
                int shortcut_x = x + m->width - MENU_PADDING - (int)strlen(item->shortcut) * 8;
                gui_draw_text(win, shortcut_x, item_y + 5, item->shortcut, text_color);
            }

            // Draw submenu arrow
            if (item->submenu) {
                gui_draw_text(win, x + m->width - MENU_PADDING - 8, item_y + 5, ">", text_color);
            }

            item_y += MENU_ITEM_HEIGHT;
        }
    }

    // Paint visible submenus
    for (int i = 0; i < m->item_count; i++) {
        if (m->items[i].submenu && menu_is_visible(m->items[i].submenu)) {
            menu_paint(m->items[i].submenu, win);
        }
    }
}
