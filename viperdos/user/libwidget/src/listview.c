//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file listview.c
 * @brief Scrollable list widget implementation for the libwidget toolkit.
 *
 * This file implements a list view widget that displays a scrollable list of
 * text items with selection support. The ListView is one of the most versatile
 * widgets, commonly used for:
 * - File browsers and directory listings
 * - Menu and option selection
 * - Log viewers and message lists
 * - Selection dialogs
 *
 * ## Visual Design
 *
 * The list view consists of:
 * 1. **Sunken Frame**: A 3D sunken border indicating an interactive content area
 * 2. **Item List**: Rows of text items, each 18 pixels tall
 * 3. **Selection Highlight**: Blue background for selected item(s)
 * 4. **Scrollbar**: Integrated vertical scrollbar when items exceed visible area
 *
 * ## Selection Modes
 *
 * The ListView supports two selection modes:
 * - **Single Selection** (default): Only one item can be selected at a time.
 *   Selection is tracked via `selected_index`.
 * - **Multi Selection**: Multiple items can be selected independently.
 *   Each item has its own selection state in the `selected` array.
 *
 * ## Dynamic Array Management
 *
 * Items are stored in a dynamically growing array. The initial capacity is 16
 * items, and the array doubles in size when full. This amortizes the cost of
 * insertions to O(1) average time.
 *
 * ## Keyboard Navigation
 *
 * When focused, the list view responds to:
 * - **Up/Down arrows**: Move selection by one item
 * - **Page Up/Down**: Move selection by a screenful
 * - **Home/End**: Jump to first/last item
 * - **Enter**: Trigger double-click callback (for "open" actions)
 *
 * @see widget.h for the listview_t structure definition
 */
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Height of each list item row in pixels.
 *
 * Each item occupies exactly 18 pixels of vertical space, providing room for
 * a single line of text (10 pixels) plus 4 pixels padding above and below.
 */
#define ITEM_HEIGHT      18

/**
 * @brief Initial capacity of the items array.
 *
 * The list starts with space for 16 items. When this capacity is exceeded,
 * the array doubles in size (16 → 32 → 64 → ...).
 */
#define INITIAL_CAPACITY 16

//===----------------------------------------------------------------------===//
// ListView Paint Handler
//===----------------------------------------------------------------------===//

/**
 * @brief Renders the list view with frame, items, selection, and scrollbar.
 *
 * This paint handler draws the complete list view visual representation:
 *
 * 1. **Sunken Frame**: A 3D sunken border around the entire widget, drawn
 *    using draw_3d_sunken(). The frame consumes 2 pixels on each edge.
 *
 * 2. **White Background**: The content area is filled with white to provide
 *    a clean, readable background for item text.
 *
 * 3. **Visible Items**: Only items within the visible viewport are drawn.
 *    The viewport starts at `scroll_offset` and shows `visible_items` rows.
 *
 * 4. **Selection Highlight**: Selected items are drawn with a blue background
 *    and white text, providing clear visual feedback.
 *
 * 5. **Scrollbar**: When the total item count exceeds visible items, a vertical
 *    scrollbar appears on the right edge. The scrollbar consists of:
 *    - A medium gray track (14 pixels wide)
 *    - A raised 3D thumb whose size is proportional to visible/total items
 *
 * ## Scrollbar Calculations
 *
 * ```
 * thumb_height = (visible_items * track_height) / item_count
 * thumb_y = track_y + (scroll_offset * (track_height - thumb_height)) /
 *           (item_count - visible_items)
 * ```
 *
 * The thumb has a minimum height of 20 pixels to remain grabbable.
 *
 * @param w   Pointer to the base widget structure (cast to listview_t internally).
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note This function updates `visible_items` as a side effect, calculating
 *       how many complete rows fit in the current widget height.
 *
 * @note Disabled list views show items in medium gray text to indicate the
 *       non-interactive state.
 *
 * @see draw_3d_sunken() For the frame rendering
 * @see draw_3d_raised() For the scrollbar thumb rendering
 */
static void listview_paint(widget_t *w, gui_window_t *win) {
    listview_t *lv = (listview_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw sunken frame
    draw_3d_sunken(win, x, y, width, height, WB_WHITE, WB_WHITE, WB_GRAY_DARK);

    // Fill background
    gui_fill_rect(win, x + 2, y + 2, width - 4, height - 4, WB_WHITE);

    // Calculate visible items
    int content_height = height - 4;
    lv->visible_items = content_height / ITEM_HEIGHT;

    // Draw items
    int item_y = y + 2;
    for (int i = 0; i < lv->visible_items && (lv->scroll_offset + i) < lv->item_count; i++) {
        int item_index = lv->scroll_offset + i;

        bool is_selected = false;
        if (lv->multi_select && lv->selected) {
            is_selected = lv->selected[item_index];
        } else {
            is_selected = (item_index == lv->selected_index);
        }

        // Draw selection highlight
        if (is_selected) {
            gui_fill_rect(win, x + 2, item_y, width - 4, ITEM_HEIGHT, WB_BLUE);
        }

        // Draw item text
        uint32_t text_color = is_selected ? WB_WHITE : WB_BLACK;
        if (!w->enabled) {
            text_color = WB_GRAY_MED;
        }

        if (lv->items[item_index]) {
            gui_draw_text(win, x + 6, item_y + 4, lv->items[item_index], text_color);
        }

        item_y += ITEM_HEIGHT;
    }

    // Draw scrollbar if needed
    if (lv->item_count > lv->visible_items) {
        int sb_x = x + width - 16;
        int sb_y = y + 2;
        int sb_height = height - 4;

        // Scrollbar track
        gui_fill_rect(win, sb_x, sb_y, 14, sb_height, WB_GRAY_MED);

        // Scrollbar thumb
        int thumb_height = (lv->visible_items * sb_height) / lv->item_count;
        if (thumb_height < 20)
            thumb_height = 20;

        int thumb_y = sb_y + (lv->scroll_offset * (sb_height - thumb_height)) /
                                 (lv->item_count - lv->visible_items);

        draw_3d_raised(win, sb_x + 1, thumb_y, 12, thumb_height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);
    }
}

//===----------------------------------------------------------------------===//
// ListView Event Handlers
//===----------------------------------------------------------------------===//

/**
 * @brief Handles mouse click events on the list view.
 *
 * This handler processes left-button clicks to either:
 * 1. Interact with the scrollbar (if clicked on the scrollbar area)
 * 2. Select an item (if clicked on the item list)
 *
 * ## Scrollbar Interaction
 *
 * If the click is in the rightmost 16 pixels and a scrollbar is visible,
 * the click is interpreted as a scrollbar interaction. The scroll position
 * is adjusted to center the view around the clicked location.
 *
 * ## Item Selection
 *
 * If the click is in the item area:
 * - **Single select mode**: The clicked item becomes the sole selection,
 *   replacing any previous selection.
 * - **Multi select mode**: The clicked item's selection is toggled—if it was
 *   selected, it becomes unselected, and vice versa.
 *
 * After selection changes, the `on_select` callback is invoked (if registered)
 * with the newly selected item index.
 *
 * @param w      Pointer to the base widget structure (cast to listview_t internally).
 * @param x      X coordinate of the click in widget-local space.
 * @param y      Y coordinate of the click in widget-local space.
 * @param button Mouse button identifier. Only left click (button 0) is processed;
 *               other buttons are ignored.
 *
 * @note Clicks on items that don't exist (beyond the item count) are ignored.
 *
 * @see listview_set_onselect() To register the selection callback
 */
static void listview_click(widget_t *w, int x, int y, int button) {
    if (button != 0)
        return;

    listview_t *lv = (listview_t *)w;

    // Check if click is on scrollbar
    if (x > w->width - 16 && lv->item_count > lv->visible_items) {
        // Scrollbar click - simplified handling
        int content_height = w->height - 4;
        int click_ratio = y * lv->item_count / content_height;
        lv->scroll_offset = click_ratio - lv->visible_items / 2;
        if (lv->scroll_offset < 0)
            lv->scroll_offset = 0;
        if (lv->scroll_offset > lv->item_count - lv->visible_items)
            lv->scroll_offset = lv->item_count - lv->visible_items;
        return;
    }

    // Calculate which item was clicked
    int item_y = y - 2;
    int clicked_item = lv->scroll_offset + item_y / ITEM_HEIGHT;

    if (clicked_item >= 0 && clicked_item < lv->item_count) {
        if (lv->multi_select && lv->selected) {
            lv->selected[clicked_item] = !lv->selected[clicked_item];
        } else {
            lv->selected_index = clicked_item;
        }

        if (lv->on_select) {
            lv->on_select(clicked_item, lv->callback_data);
        }
    }
}

/**
 * @brief Handles keyboard events for list navigation.
 *
 * This handler processes navigation keys to move the selection within the list.
 * All navigation operations automatically scroll the view to keep the selected
 * item visible (via listview_ensure_visible()).
 *
 * ## Supported Keys
 *
 * | Key       | Keycode | Action                                    |
 * |-----------|---------|-------------------------------------------|
 * | Up Arrow  | 0x52    | Move selection to previous item           |
 * | Down Arrow| 0x51    | Move selection to next item               |
 * | Page Up   | 0x4B    | Move selection up by one screenful        |
 * | Page Down | 0x4E    | Move selection down by one screenful      |
 * | Home      | 0x4A    | Jump to first item                        |
 * | End       | 0x4D    | Jump to last item                         |
 * | Enter     | 0x28    | Trigger double-click callback             |
 *
 * After each navigation, the `on_select` callback is invoked (if registered)
 * with the new selection index. For Enter, the `on_double_click` callback is
 * invoked instead.
 *
 * @param w       Pointer to the base widget structure (cast to listview_t internally).
 * @param keycode The USB HID keycode of the pressed key.
 * @param ch      The character representation of the key (unused).
 *
 * @note In multi-select mode, keyboard navigation affects only the "focus"
 *       item (tracked by selected_index), not the selection state.
 *
 * @note Selection is clamped to valid bounds (0 to item_count-1).
 *
 * @see listview_ensure_visible() Which scrolls to show the selected item
 */
static void listview_key(widget_t *w, int keycode, char ch) {
    (void)ch;
    listview_t *lv = (listview_t *)w;

    switch (keycode) {
    case 0x52: // Up arrow
        if (lv->selected_index > 0) {
            lv->selected_index--;
            listview_ensure_visible(lv, lv->selected_index);
            if (lv->on_select) {
                lv->on_select(lv->selected_index, lv->callback_data);
            }
        }
        break;

    case 0x51: // Down arrow
        if (lv->selected_index < lv->item_count - 1) {
            lv->selected_index++;
            listview_ensure_visible(lv, lv->selected_index);
            if (lv->on_select) {
                lv->on_select(lv->selected_index, lv->callback_data);
            }
        }
        break;

    case 0x4B: // Page Up
        lv->selected_index -= lv->visible_items;
        if (lv->selected_index < 0)
            lv->selected_index = 0;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x4E: // Page Down
        lv->selected_index += lv->visible_items;
        if (lv->selected_index >= lv->item_count)
            lv->selected_index = lv->item_count - 1;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x4A: // Home
        lv->selected_index = 0;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x4D: // End
        lv->selected_index = lv->item_count - 1;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x28: // Enter
        if (lv->on_double_click && lv->selected_index >= 0) {
            lv->on_double_click(lv->selected_index, lv->callback_data);
        }
        break;
    }
}

//===----------------------------------------------------------------------===//
// ListView API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new list view widget.
 *
 * This function allocates and initializes a list view with an empty item list.
 * The list view is ready to have items added via listview_add_item().
 *
 * Default properties:
 * - **Size**: 200x150 pixels
 * - **Position**: (0, 0) - use widget_set_position() to place
 * - **Colors**: White background, black text
 * - **Selection**: Single-select mode, no item selected (index = -1)
 * - **Scrolling**: Starts at top (scroll_offset = 0)
 * - **Capacity**: Initial space for 16 items (grows as needed)
 *
 * ## Memory Management
 *
 * The list view allocates an initial items array with capacity for 16 items.
 * This array grows automatically when more items are added. When destroyed,
 * the list view frees all item strings and the items array.
 *
 * @param parent Pointer to the parent widget container. If non-NULL, the list
 *               view is added to this parent's child list. Pass NULL for list
 *               views that will be added to a parent later.
 *
 * @return Pointer to the newly created list view, or NULL if memory allocation
 *         failed. The returned pointer can be cast to widget_t* for use with
 *         generic widget functions.
 *
 * @code
 * // Create a file list
 * listview_t *files = listview_create(dialog);
 * widget_set_position((widget_t *)files, 10, 40);
 * widget_set_size((widget_t *)files, 280, 200);
 *
 * // Populate with files
 * listview_add_item(files, "Document.txt");
 * listview_add_item(files, "Image.png");
 * listview_add_item(files, "Readme.md");
 *
 * // Select first item by default
 * listview_set_selected(files, 0);
 * @endcode
 *
 * @see listview_add_item() To add items to the list
 * @see listview_set_onselect() To handle selection changes
 * @see widget_destroy() To free the list view when done
 */
listview_t *listview_create(widget_t *parent) {
    listview_t *lv = (listview_t *)malloc(sizeof(listview_t));
    if (!lv)
        return NULL;

    memset(lv, 0, sizeof(listview_t));

    // Initialize base widget
    lv->base.type = WIDGET_LISTVIEW;
    lv->base.parent = parent;
    lv->base.visible = true;
    lv->base.enabled = true;
    lv->base.bg_color = WB_WHITE;
    lv->base.fg_color = WB_BLACK;
    lv->base.width = 200;
    lv->base.height = 150;

    // Set handlers
    lv->base.on_paint = listview_paint;
    lv->base.on_click = listview_click;
    lv->base.on_key = listview_key;

    // Initialize items array
    lv->item_capacity = INITIAL_CAPACITY;
    lv->items = (char **)malloc(lv->item_capacity * sizeof(char *));
    if (!lv->items) {
        free(lv);
        return NULL;
    }
    lv->item_count = 0;
    lv->selected_index = -1;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)lv);
    }

    return lv;
}

/**
 * @brief Appends a new item to the end of the list.
 *
 * This function adds a text item at the end of the list's item array. The text
 * is copied (via strdup) so the original string can be freed after this call.
 *
 * If the items array is full, it is automatically doubled in size to accommodate
 * the new item. This provides O(1) amortized insertion time.
 *
 * @param lv   Pointer to the list view widget. If NULL, does nothing.
 * @param text The text string for the new item. If NULL, does nothing.
 *             The string is duplicated internally.
 *
 * @note In multi-select mode, the new item is initially unselected.
 *
 * @note This function does not trigger a repaint. Call widget_app_repaint()
 *       to see the new item.
 *
 * @see listview_insert_item() To insert at a specific position
 * @see listview_remove_item() To remove an item
 */
void listview_add_item(listview_t *lv, const char *text) {
    if (!lv || !text)
        return;

    // Grow array if needed
    if (lv->item_count >= lv->item_capacity) {
        int new_cap = lv->item_capacity * 2;
        char **new_items = (char **)realloc(lv->items, new_cap * sizeof(char *));
        if (!new_items)
            return;
        lv->items = new_items;
        lv->item_capacity = new_cap;

        // Also grow selection array if multi-select
        if (lv->multi_select && lv->selected) {
            bool *new_selected = (bool *)realloc(lv->selected, new_cap * sizeof(bool));
            if (new_selected) {
                lv->selected = new_selected;
            }
        }
    }

    lv->items[lv->item_count] = strdup(text);
    if (lv->multi_select && lv->selected) {
        lv->selected[lv->item_count] = false;
    }
    lv->item_count++;
}

/**
 * @brief Inserts an item at a specific position in the list.
 *
 * This function inserts a new item at the specified index, shifting all
 * subsequent items down by one position. If the index is beyond the current
 * item count, the item is appended to the end (equivalent to listview_add_item).
 *
 * @param lv    Pointer to the list view widget. If NULL, does nothing.
 * @param index The zero-based position at which to insert the new item.
 *              If negative, does nothing. If >= item_count, appends to end.
 * @param text  The text string for the new item. If NULL, does nothing.
 *
 * @note If the current selection is at or after the insertion point, the
 *       selected_index is incremented to maintain selection of the same item.
 *
 * @see listview_add_item() To append at the end
 * @see listview_remove_item() To remove an item
 */
void listview_insert_item(listview_t *lv, int index, const char *text) {
    if (!lv || !text || index < 0)
        return;

    if (index >= lv->item_count) {
        listview_add_item(lv, text);
        return;
    }

    // Grow array if needed
    if (lv->item_count >= lv->item_capacity) {
        int new_cap = lv->item_capacity * 2;
        char **new_items = (char **)realloc(lv->items, new_cap * sizeof(char *));
        if (!new_items)
            return;
        lv->items = new_items;
        lv->item_capacity = new_cap;
    }

    // Shift items
    memmove(&lv->items[index + 1], &lv->items[index], (lv->item_count - index) * sizeof(char *));
    lv->items[index] = strdup(text);
    lv->item_count++;

    // Adjust selection
    if (lv->selected_index >= index) {
        lv->selected_index++;
    }
}

/**
 * @brief Removes an item from the list at the specified index.
 *
 * This function removes the item at the given index, shifting all subsequent
 * items up by one position. The memory allocated for the item's text string
 * is freed.
 *
 * @param lv    Pointer to the list view widget. If NULL, does nothing.
 * @param index The zero-based index of the item to remove. If out of range
 *              (negative or >= item_count), does nothing.
 *
 * @note If the removed item was selected, the selection moves to the previous
 *       item (or clears if the list becomes empty).
 *
 * @note If the removed item was before the selected item, selected_index is
 *       decremented to maintain selection of the same item.
 *
 * @see listview_clear() To remove all items at once
 */
void listview_remove_item(listview_t *lv, int index) {
    if (!lv || index < 0 || index >= lv->item_count)
        return;

    free(lv->items[index]);

    // Shift items
    memmove(&lv->items[index], &lv->items[index + 1], (lv->item_count - index - 1) * sizeof(char *));
    lv->item_count--;

    // Adjust selection
    if (lv->selected_index >= lv->item_count) {
        lv->selected_index = lv->item_count - 1;
    }
}

/**
 * @brief Removes all items from the list.
 *
 * This function frees all item strings and resets the list to an empty state.
 * The items array capacity is retained for future use (not shrunk).
 *
 * After clearing:
 * - item_count is 0
 * - selected_index is -1 (no selection)
 * - scroll_offset is 0 (scrolled to top)
 *
 * @param lv Pointer to the list view widget. If NULL, does nothing.
 *
 * @see listview_remove_item() To remove a single item
 */
void listview_clear(listview_t *lv) {
    if (!lv)
        return;

    for (int i = 0; i < lv->item_count; i++) {
        free(lv->items[i]);
    }
    lv->item_count = 0;
    lv->selected_index = -1;
    lv->scroll_offset = 0;
}

/**
 * @brief Returns the number of items in the list.
 *
 * @param lv Pointer to the list view widget. If NULL, returns 0.
 *
 * @return The total number of items in the list, or 0 if lv is NULL.
 */
int listview_get_count(listview_t *lv) {
    return lv ? lv->item_count : 0;
}

/**
 * @brief Retrieves the text of an item at the specified index.
 *
 * @param lv    Pointer to the list view widget. If NULL, returns NULL.
 * @param index The zero-based index of the item.
 *
 * @return Pointer to the item's text string (read-only), or NULL if the
 *         index is out of range or lv is NULL. The returned pointer points
 *         to internal storage and remains valid until the item is removed
 *         or modified.
 *
 * @see listview_set_item() To change an item's text
 */
const char *listview_get_item(listview_t *lv, int index) {
    if (!lv || index < 0 || index >= lv->item_count)
        return NULL;
    return lv->items[index];
}

/**
 * @brief Changes the text of an existing item.
 *
 * This function replaces the text of the item at the specified index. The
 * old text is freed, and the new text is duplicated internally.
 *
 * @param lv    Pointer to the list view widget. If NULL, does nothing.
 * @param index The zero-based index of the item to modify.
 * @param text  The new text string. If NULL, does nothing.
 *
 * @note This function does not trigger a repaint.
 *
 * @see listview_get_item() To retrieve an item's text
 */
void listview_set_item(listview_t *lv, int index, const char *text) {
    if (!lv || !text || index < 0 || index >= lv->item_count)
        return;

    free(lv->items[index]);
    lv->items[index] = strdup(text);
}

/**
 * @brief Returns the index of the currently selected item.
 *
 * In single-select mode, this returns the index of the one selected item.
 * In multi-select mode, this returns the index of the "focused" item (the
 * item that receives keyboard navigation), which may or may not be selected.
 *
 * @param lv Pointer to the list view widget. If NULL, returns -1.
 *
 * @return The zero-based index of the selected/focused item, or -1 if no
 *         item is selected or lv is NULL.
 *
 * @see listview_set_selected() To change the selection
 */
int listview_get_selected(listview_t *lv) {
    return lv ? lv->selected_index : -1;
}

/**
 * @brief Programmatically selects an item by index.
 *
 * This function changes the selected item without triggering the on_select
 * callback. It also scrolls the view if necessary to make the selected item
 * visible.
 *
 * @param lv    Pointer to the list view widget. If NULL, does nothing.
 * @param index The zero-based index of the item to select. Use -1 to clear
 *              the selection. Out-of-range values are clamped to valid bounds.
 *
 * @note Unlike user selection via click or keyboard, this function does NOT
 *       invoke the on_select callback.
 *
 * @see listview_get_selected() To retrieve the current selection
 * @see listview_ensure_visible() Which is called internally to scroll
 */
void listview_set_selected(listview_t *lv, int index) {
    if (!lv)
        return;

    if (index < -1)
        index = -1;
    if (index >= lv->item_count)
        index = lv->item_count - 1;

    lv->selected_index = index;
    if (index >= 0) {
        listview_ensure_visible(lv, index);
    }
}

/**
 * @brief Registers a callback for selection change events.
 *
 * The on_select callback is invoked whenever the user selects an item by
 * clicking on it or using keyboard navigation. The callback receives the
 * index of the newly selected item.
 *
 * @param lv       Pointer to the list view widget. If NULL, does nothing.
 * @param callback The function to call when selection changes, or NULL to
 *                 remove any existing callback. Signature: void (*)(int, void*)
 * @param data     User-defined data passed to the callback function.
 *
 * @note The callback is NOT invoked for programmatic selection changes via
 *       listview_set_selected().
 *
 * @code
 * static void on_file_selected(int index, void *data) {
 *     listview_t *lv = (listview_t *)data;
 *     const char *filename = listview_get_item(lv, index);
 *     printf("Selected: %s\n", filename);
 * }
 *
 * listview_set_onselect(files, on_file_selected, files);
 * @endcode
 *
 * @see listview_set_ondoubleclick() For handling "open" actions
 */
void listview_set_onselect(listview_t *lv, listview_select_fn callback, void *data) {
    if (lv) {
        lv->on_select = callback;
        lv->callback_data = data;
    }
}

/**
 * @brief Registers a callback for double-click (or Enter key) events.
 *
 * The on_double_click callback is invoked when the user presses Enter on
 * a selected item. This is typically used for "open" or "activate" actions.
 *
 * @param lv       Pointer to the list view widget. If NULL, does nothing.
 * @param callback The function to call on double-click/Enter, or NULL to
 *                 remove any existing callback.
 * @param data     User-defined data passed to the callback function.
 *
 * @note This uses the same callback_data as on_select. Setting either callback
 *       updates the shared data pointer.
 *
 * @see listview_set_onselect() For handling selection changes
 */
void listview_set_ondoubleclick(listview_t *lv, listview_select_fn callback, void *data) {
    if (lv) {
        lv->on_double_click = callback;
        lv->callback_data = data;
    }
}

/**
 * @brief Scrolls the view to ensure a specific item is visible.
 *
 * If the item at the given index is currently outside the visible viewport,
 * this function adjusts scroll_offset to bring it into view:
 * - If the item is above the visible area, it scrolls up to show the item
 *   at the top of the viewport.
 * - If the item is below the visible area, it scrolls down to show the item
 *   at the bottom of the viewport.
 * - If the item is already visible, no scrolling occurs.
 *
 * @param lv    Pointer to the list view widget. If NULL, does nothing.
 * @param index The zero-based index of the item to make visible.
 *
 * @note This function is called automatically by keyboard navigation and
 *       listview_set_selected().
 */
void listview_ensure_visible(listview_t *lv, int index) {
    if (!lv || index < 0 || index >= lv->item_count)
        return;

    if (index < lv->scroll_offset) {
        lv->scroll_offset = index;
    } else if (index >= lv->scroll_offset + lv->visible_items) {
        lv->scroll_offset = index - lv->visible_items + 1;
    }
}
