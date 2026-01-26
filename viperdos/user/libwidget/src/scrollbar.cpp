//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file scrollbar.c
 * @brief Scrollbar widget implementation for the libwidget toolkit.
 *
 * This file implements horizontal and vertical scrollbar widgets that allow
 * users to navigate content that is larger than its visible viewport.
 * Scrollbars are essential for:
 * - Text editors (scrolling through long documents)
 * - List views (scrolling through many items)
 * - Image viewers (panning large images)
 * - Any scrollable content area
 *
 * ## Scrollbar Anatomy
 *
 * A scrollbar consists of three parts:
 *
 * 1. **Arrow Buttons**: Two 16x16 pixel buttons at the ends that increment
 *    or decrement the scroll position by 1 when clicked.
 *    - Vertical: Up arrow at top, down arrow at bottom
 *    - Horizontal: Left arrow at left, right arrow at right
 *
 * 2. **Track**: The medium gray area between the arrow buttons. Clicking
 *    on the track jumps the scroll position to that location.
 *
 * 3. **Thumb**: A raised 3D rectangle that represents the current viewport
 *    position within the total content. The thumb's size is proportional
 *    to the page_size relative to the total range.
 *
 * ## Value Model
 *
 * The scrollbar tracks a value within a range:
 * - **min_val**: Minimum scroll position (typically 0)
 * - **max_val**: Maximum scroll position (total content - viewport)
 * - **value**: Current scroll position (clamped to min..max)
 * - **page_size**: Size of the visible viewport (affects thumb size)
 *
 * ## Thumb Size Calculation
 *
 * The thumb size is proportional to how much of the content is visible:
 * ```
 * thumb_size = (page_size * track_size) / (range + page_size)
 * ```
 *
 * The thumb position maps the current value to the track position:
 * ```
 * thumb_position = (value - min_val) * (track_size - thumb_size) / range
 * ```
 *
 * The thumb has a minimum size of 20 pixels to remain easily grabbable.
 *
 * @see widget.h for the scrollbar_t structure definition
 */
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Size of the arrow buttons at each end of the scrollbar in pixels.
 *
 * Both horizontal and vertical scrollbars use 16-pixel square arrow buttons.
 * The track area is the scrollbar length minus 2 * ARROW_SIZE.
 */
#define ARROW_SIZE   16

/**
 * @brief Minimum size of the thumb in pixels.
 *
 * Even when the viewport shows only a tiny fraction of the content, the thumb
 * is at least 20 pixels so it remains easy to click and drag.
 */
#define MIN_THUMB    20

//===----------------------------------------------------------------------===//
// Scrollbar Paint Handler
//===----------------------------------------------------------------------===//

/**
 * @brief Renders the scrollbar with track, thumb, and arrow buttons.
 *
 * This paint handler draws the complete scrollbar visual representation.
 * The rendering differs based on orientation:
 *
 * ## Vertical Scrollbar
 *
 * ```
 * +---+
 * | ^ |  <- Up arrow button (16px)
 * +---+
 * |   |
 * |===|  <- Thumb (proportional size)
 * |   |
 * +---+
 * | v |  <- Down arrow button (16px)
 * +---+
 * ```
 *
 * ## Horizontal Scrollbar
 *
 * ```
 * +---+-----+===+-----+---+
 * | < |     |   |     | > |
 * +---+-----+===+-----+---+
 *  ^    ^     ^    ^    ^
 *  |    |     |    |    +-- Right arrow
 *  |    |     |    +-- Track (after thumb)
 *  |    |     +-- Thumb
 *  |    +-- Track (before thumb)
 *  +-- Left arrow
 * ```
 *
 * ## Drawing Components
 *
 * 1. **Track**: Filled with medium gray (WB_GRAY_MED)
 * 2. **Arrow Buttons**: Raised 3D buttons with "^", "v", "<", or ">" text
 * 3. **Thumb**: Raised 3D rectangle positioned based on current value
 *
 * @param w   Pointer to the base widget structure (cast to scrollbar_t internally).
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note The thumb size and position are calculated from min_val, max_val,
 *       value, and page_size. If the range is zero or negative, the thumb
 *       fills the entire track.
 */
static void scrollbar_paint(widget_t *w, gui_window_t *win) {
    scrollbar_t *sb = (scrollbar_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    if (sb->vertical) {
        // Vertical scrollbar
        // Draw track background
        gui_fill_rect(win, x, y, width, height, WB_GRAY_MED);

        // Draw top arrow button
        draw_3d_raised(win, x, y, width, ARROW_SIZE, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);
        gui_draw_text(win, x + width / 2 - 4, y + 3, "^", WB_BLACK);

        // Draw bottom arrow button
        draw_3d_raised(win, x, y + height - ARROW_SIZE, width, ARROW_SIZE, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
        gui_draw_text(win, x + width / 2 - 4, y + height - ARROW_SIZE + 3, "v", WB_BLACK);

        // Calculate thumb
        int track_start = y + ARROW_SIZE;
        int track_height = height - ARROW_SIZE * 2;

        int range = sb->max_val - sb->min_val;
        int thumb_height = MIN_THUMB;
        int thumb_y = track_start;

        if (range > 0 && sb->page_size > 0) {
            thumb_height = (sb->page_size * track_height) / (range + sb->page_size);
            if (thumb_height < MIN_THUMB)
                thumb_height = MIN_THUMB;
            if (thumb_height > track_height)
                thumb_height = track_height;

            thumb_y = track_start +
                      ((sb->value - sb->min_val) * (track_height - thumb_height)) / range;
        }

        // Draw thumb
        draw_3d_raised(win, x + 1, thumb_y, width - 2, thumb_height, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
    } else {
        // Horizontal scrollbar
        // Draw track background
        gui_fill_rect(win, x, y, width, height, WB_GRAY_MED);

        // Draw left arrow button
        draw_3d_raised(win, x, y, ARROW_SIZE, height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);
        gui_draw_text(win, x + 4, y + height / 2 - 5, "<", WB_BLACK);

        // Draw right arrow button
        draw_3d_raised(win, x + width - ARROW_SIZE, y, ARROW_SIZE, height, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
        gui_draw_text(win, x + width - ARROW_SIZE + 4, y + height / 2 - 5, ">", WB_BLACK);

        // Calculate thumb
        int track_start = x + ARROW_SIZE;
        int track_width = width - ARROW_SIZE * 2;

        int range = sb->max_val - sb->min_val;
        int thumb_width = MIN_THUMB;
        int thumb_x = track_start;

        if (range > 0 && sb->page_size > 0) {
            thumb_width = (sb->page_size * track_width) / (range + sb->page_size);
            if (thumb_width < MIN_THUMB)
                thumb_width = MIN_THUMB;
            if (thumb_width > track_width)
                thumb_width = track_width;

            thumb_x = track_start +
                      ((sb->value - sb->min_val) * (track_width - thumb_width)) / range;
        }

        // Draw thumb
        draw_3d_raised(win, thumb_x, y + 1, thumb_width, height - 2, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
    }
}

//===----------------------------------------------------------------------===//
// Scrollbar Event Handlers
//===----------------------------------------------------------------------===//

/**
 * @brief Handles mouse click events on the scrollbar.
 *
 * This handler processes left-button clicks to scroll the content. The behavior
 * depends on where the click occurred:
 *
 * ## Click Regions (Vertical)
 *
 * | Region          | Action                                      |
 * |-----------------|---------------------------------------------|
 * | Top arrow       | Decrement value by 1 (scroll up)            |
 * | Bottom arrow    | Increment value by 1 (scroll down)          |
 * | Track area      | Jump to clicked position                    |
 *
 * ## Click Regions (Horizontal)
 *
 * | Region          | Action                                      |
 * |-----------------|---------------------------------------------|
 * | Left arrow      | Decrement value by 1 (scroll left)          |
 * | Right arrow     | Increment value by 1 (scroll right)         |
 * | Track area      | Jump to clicked position                    |
 *
 * When the value changes, the on_change callback is invoked (if registered).
 *
 * @param w       Pointer to the base widget structure (cast to scrollbar_t internally).
 * @param click_x X coordinate of the click in widget-local space.
 * @param click_y Y coordinate of the click in widget-local space.
 * @param button  Mouse button identifier. Only left click (button 0) is processed.
 *
 * @note Clicking the track directly sets the value to correspond to the click
 *       position, rather than page-scrolling.
 *
 * @note The value is always clamped to the valid range (min_val to max_val).
 *
 * @see scrollbar_set_onchange() To register the value change callback
 */
static void scrollbar_click(widget_t *w, int click_x, int click_y, int button) {
    if (button != 0)
        return;

    scrollbar_t *sb = (scrollbar_t *)w;

    int range = sb->max_val - sb->min_val;
    if (range <= 0)
        return;

    int new_value = sb->value;

    if (sb->vertical) {
        int height = w->height;

        if (click_y < ARROW_SIZE) {
            // Top arrow - scroll up
            new_value -= 1;
        } else if (click_y >= height - ARROW_SIZE) {
            // Bottom arrow - scroll down
            new_value += 1;
        } else {
            // Track click - page scroll or direct position
            int track_height = height - ARROW_SIZE * 2;
            int track_y = click_y - ARROW_SIZE;

            new_value = sb->min_val + (track_y * range) / track_height;
        }
    } else {
        int width = w->width;

        if (click_x < ARROW_SIZE) {
            // Left arrow - scroll left
            new_value -= 1;
        } else if (click_x >= width - ARROW_SIZE) {
            // Right arrow - scroll right
            new_value += 1;
        } else {
            // Track click - page scroll or direct position
            int track_width = width - ARROW_SIZE * 2;
            int track_x = click_x - ARROW_SIZE;

            new_value = sb->min_val + (track_x * range) / track_width;
        }
    }

    // Clamp value
    if (new_value < sb->min_val)
        new_value = sb->min_val;
    if (new_value > sb->max_val)
        new_value = sb->max_val;

    if (new_value != sb->value) {
        sb->value = new_value;
        if (sb->on_change) {
            sb->on_change(sb->callback_data);
        }
    }
}

//===----------------------------------------------------------------------===//
// Scrollbar API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new scrollbar widget.
 *
 * This function allocates and initializes a scrollbar with default settings.
 * The scrollbar can be either horizontal or vertical based on the parameter.
 *
 * Default properties:
 * - **Size**: 16x100 (vertical) or 100x16 (horizontal)
 * - **Range**: 0 to 100
 * - **Value**: 0 (scrolled to beginning)
 * - **Page Size**: 10 (affects thumb size)
 * - **Colors**: Medium gray track, light gray buttons and thumb
 *
 * @param parent   Pointer to the parent widget container. If non-NULL, the
 *                 scrollbar is added to this parent's child list.
 * @param vertical True for a vertical scrollbar (up/down), false for
 *                 horizontal (left/right).
 *
 * @return Pointer to the newly created scrollbar, or NULL if memory allocation
 *         failed.
 *
 * @code
 * // Create a vertical scrollbar for a text area
 * scrollbar_t *vscroll = scrollbar_create(panel, true);
 * widget_set_position((widget_t *)vscroll, text_width, 0);
 * widget_set_size((widget_t *)vscroll, 16, text_height);
 *
 * // Configure for 1000 lines with 20 visible
 * scrollbar_set_range(vscroll, 0, 1000 - 20);
 * scrollbar_set_page_size(vscroll, 20);
 * scrollbar_set_onchange(vscroll, on_scroll, text_area);
 * @endcode
 *
 * @see scrollbar_set_range() To configure the scroll range
 * @see scrollbar_set_page_size() To set the viewport size
 * @see scrollbar_set_onchange() To handle scroll events
 */
scrollbar_t *scrollbar_create(widget_t *parent, bool vertical) {
    scrollbar_t *sb = (scrollbar_t *)malloc(sizeof(scrollbar_t));
    if (!sb)
        return NULL;

    memset(sb, 0, sizeof(scrollbar_t));

    // Initialize base widget
    sb->base.type = WIDGET_SCROLLBAR;
    sb->base.parent = parent;
    sb->base.visible = true;
    sb->base.enabled = true;
    sb->base.bg_color = WB_GRAY_MED;
    sb->base.fg_color = WB_BLACK;

    if (vertical) {
        sb->base.width = 16;
        sb->base.height = 100;
    } else {
        sb->base.width = 100;
        sb->base.height = 16;
    }

    // Set handlers
    sb->base.on_paint = scrollbar_paint;
    sb->base.on_click = scrollbar_click;

    sb->vertical = vertical;
    sb->min_val = 0;
    sb->max_val = 100;
    sb->value = 0;
    sb->page_size = 10;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)sb);
    }

    return sb;
}

/**
 * @brief Sets the current scroll position.
 *
 * This function updates the scrollbar's current value, which determines the
 * thumb position. The value is automatically clamped to the valid range.
 *
 * @param sb    Pointer to the scrollbar widget. If NULL, does nothing.
 * @param value The new scroll position. Values outside the range are clamped
 *              to min_val or max_val as appropriate.
 *
 * @note This function does NOT trigger the on_change callback.
 *
 * @note Trigger a repaint to see the visual change.
 *
 * @see scrollbar_get_value() To retrieve the current value
 * @see scrollbar_set_range() To change the valid range
 */
void scrollbar_set_value(scrollbar_t *sb, int value) {
    if (!sb)
        return;

    if (value < sb->min_val)
        value = sb->min_val;
    if (value > sb->max_val)
        value = sb->max_val;

    sb->value = value;
}

/**
 * @brief Retrieves the current scroll position.
 *
 * @param sb Pointer to the scrollbar widget. If NULL, returns 0.
 *
 * @return The current scroll value, or 0 if sb is NULL.
 *
 * @see scrollbar_set_value() To change the current value
 */
int scrollbar_get_value(scrollbar_t *sb) {
    return sb ? sb->value : 0;
}

/**
 * @brief Sets the minimum and maximum scroll range.
 *
 * This function defines the range of valid scroll positions. Typically:
 * - min_val = 0
 * - max_val = total_content_size - viewport_size
 *
 * If the current value is outside the new range, it is clamped to fit.
 *
 * @param sb      Pointer to the scrollbar widget. If NULL, does nothing.
 * @param min_val The minimum scroll position (represents beginning of content).
 * @param max_val The maximum scroll position (represents end of content minus
 *                viewport size).
 *
 * @note If min_val >= max_val, the scrollbar will display with a full-size
 *       thumb (no scrolling needed).
 *
 * @code
 * // Configure for a 1000-line document with 25 visible lines
 * int total_lines = 1000;
 * int visible_lines = 25;
 * scrollbar_set_range(sb, 0, total_lines - visible_lines);
 * scrollbar_set_page_size(sb, visible_lines);
 * @endcode
 *
 * @see scrollbar_set_page_size() To set the viewport size
 */
void scrollbar_set_range(scrollbar_t *sb, int min_val, int max_val) {
    if (!sb)
        return;

    sb->min_val = min_val;
    sb->max_val = max_val;

    // Clamp current value
    if (sb->value < min_val)
        sb->value = min_val;
    if (sb->value > max_val)
        sb->value = max_val;
}

/**
 * @brief Sets the page (viewport) size.
 *
 * The page size affects the thumb's size proportionally. A larger page size
 * (more content visible at once) results in a larger thumb.
 *
 * The thumb size is calculated as:
 * ```
 * thumb_size = (page_size * track_size) / (range + page_size)
 * ```
 *
 * @param sb        Pointer to the scrollbar widget. If NULL, does nothing.
 * @param page_size The size of the visible viewport, in the same units as
 *                  the range (e.g., lines, pixels). Must be positive.
 *
 * @note If page_size is 0 or negative, the call is ignored.
 *
 * @code
 * // For a text editor showing 30 lines at a time
 * scrollbar_set_page_size(vscroll, 30);
 *
 * // For a pixel-based scroll of 400 visible pixels
 * scrollbar_set_page_size(hscroll, 400);
 * @endcode
 *
 * @see scrollbar_set_range() To configure the total content range
 */
void scrollbar_set_page_size(scrollbar_t *sb, int page_size) {
    if (sb && page_size > 0) {
        sb->page_size = page_size;
    }
}

/**
 * @brief Registers a callback for scroll value changes.
 *
 * The on_change callback is invoked whenever the user interacts with the
 * scrollbar to change the scroll position (clicking arrows or track).
 * The callback should update the associated content's visible region.
 *
 * @param sb       Pointer to the scrollbar widget. If NULL, does nothing.
 * @param callback The function to call when the value changes, or NULL to
 *                 remove any existing callback.
 * @param data     User-defined data passed to the callback function.
 *
 * @note The callback is NOT invoked for programmatic value changes via
 *       scrollbar_set_value().
 *
 * @code
 * static void on_scroll(void *data) {
 *     text_area_t *ta = (text_area_t *)data;
 *     int new_top = scrollbar_get_value(ta->scrollbar);
 *     text_area_scroll_to(ta, new_top);
 * }
 *
 * scrollbar_set_onchange(vscroll, on_scroll, text_area);
 * @endcode
 *
 * @see scrollbar_click() Which invokes this callback on user interaction
 */
void scrollbar_set_onchange(scrollbar_t *sb, widget_callback_fn callback, void *data) {
    if (sb) {
        sb->on_change = callback;
        sb->callback_data = data;
    }
}
