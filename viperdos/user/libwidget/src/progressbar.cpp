//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file progressbar.c
 * @brief Progress bar widget implementation for the libwidget toolkit.
 *
 * This file implements a horizontal progress bar widget that displays the
 * completion status of a task or operation. Progress bars are commonly used
 * for:
 * - File copy/download progress
 * - Installation progress
 * - Loading operations
 * - Task completion indicators
 *
 * ## Visual Design
 *
 * The progress bar consists of:
 * 1. **Sunken Frame**: A 3D sunken border (2 pixels) indicating a value display
 * 2. **Fill Bar**: A blue rectangle that grows from left to right based on value
 * 3. **Percentage Text** (optional): Centered text showing the percentage complete
 *
 * ## Value Range
 *
 * Progress is tracked using three values:
 * - **min_val**: The minimum value (typically 0)
 * - **max_val**: The maximum value (typically 100)
 * - **value**: The current progress value (clamped to min..max)
 *
 * The fill percentage is calculated as: (value - min_val) / (max_val - min_val)
 *
 * ## Usage Example
 *
 * @code
 * // Create a progress bar
 * progressbar_t *pb = progressbar_create(dialog);
 * widget_set_position((widget_t *)pb, 20, 100);
 * widget_set_size((widget_t *)pb, 300, 20);
 *
 * // Update progress during operation
 * for (int i = 0; i <= 100; i++) {
 *     progressbar_set_value(pb, i);
 *     widget_app_repaint(app);
 *     // ... do work ...
 * }
 * @endcode
 *
 * @see widget.h for the progressbar_t structure definition
 */
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <widget.h>

//===----------------------------------------------------------------------===//
// ProgressBar Paint Handler
//===----------------------------------------------------------------------===//

/**
 * @brief Renders the progress bar with frame, fill, and optional percentage text.
 *
 * This paint handler draws the complete progress bar visual representation:
 *
 * 1. **Sunken Frame**: A 3D sunken border indicating this is a value display,
 *    drawn using draw_3d_sunken(). The frame is 2 pixels on each edge.
 *
 * 2. **Progress Fill**: A blue rectangle (WB_BLUE) that fills from the left
 *    edge proportionally to the current value. The fill is drawn inside
 *    the frame (offset by 2 pixels on all sides).
 *
 * 3. **Percentage Text** (if show_text is true): A centered percentage label
 *    showing the completion as "XX%". The text is drawn in black.
 *
 * ## Fill Width Calculation
 *
 * ```
 * range = max_val - min_val
 * fill_width = ((value - min_val) * (width - 4)) / range
 * ```
 *
 * The width - 4 accounts for the 2-pixel border on each side.
 *
 * @param w   Pointer to the base widget structure (cast to progressbar_t internally).
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note If the range is zero or negative (min >= max), no fill is drawn.
 *
 * @note The percentage text is always drawn in black. For better visibility
 *       when the fill covers the text area, a more sophisticated implementation
 *       could use white text on the filled portion and black on the unfilled.
 *
 * @see progressbar_set_value() To change the displayed progress
 * @see progressbar_set_show_text() To enable/disable percentage display
 */
static void progressbar_paint(widget_t *w, gui_window_t *win) {
    progressbar_t *pb = (progressbar_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw sunken frame
    draw_3d_sunken(win, x, y, width, height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

    // Calculate fill width
    int range = pb->max_val - pb->min_val;
    int fill_width = 0;
    if (range > 0) {
        fill_width = ((pb->value - pb->min_val) * (width - 4)) / range;
    }

    // Draw progress fill
    if (fill_width > 0) {
        gui_fill_rect(win, x + 2, y + 2, fill_width, height - 4, WB_BLUE);
    }

    // Draw percentage text if enabled
    if (pb->show_text) {
        char buf[16];
        int percent = 0;
        if (range > 0) {
            percent = ((pb->value - pb->min_val) * 100) / range;
        }
        snprintf(buf, sizeof(buf), "%d%%", percent);

        int text_len = (int)strlen(buf);
        int text_x = x + (width - text_len * 8) / 2;
        int text_y = y + (height - 10) / 2;

        // Draw text - white on filled part, black on unfilled
        gui_draw_text(win, text_x, text_y, buf, WB_BLACK);
    }
}

//===----------------------------------------------------------------------===//
// ProgressBar API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new progress bar widget.
 *
 * This function allocates and initializes a progress bar with default
 * settings. The progress bar starts at 0% and is ready to display progress.
 *
 * Default properties:
 * - **Size**: 200x20 pixels
 * - **Position**: (0, 0) - use widget_set_position() to place
 * - **Range**: 0 to 100
 * - **Value**: 0 (empty)
 * - **Show Text**: true (percentage is displayed)
 * - **Colors**: Gray frame, blue fill, black text
 *
 * @param parent Pointer to the parent widget container. If non-NULL, the
 *               progress bar is added to the parent's child list. Pass NULL
 *               for progress bars managed independently.
 *
 * @return Pointer to the newly created progress bar, or NULL if memory
 *         allocation failed. The returned pointer can be cast to widget_t*
 *         for use with generic widget functions.
 *
 * @code
 * // Create a progress bar for file copy
 * progressbar_t *progress = progressbar_create(dialog);
 * widget_set_position((widget_t *)progress, 10, 50);
 * widget_set_size((widget_t *)progress, 280, 20);
 *
 * // Start with 0%
 * progressbar_set_value(progress, 0);
 * @endcode
 *
 * @see progressbar_set_value() To update the progress
 * @see progressbar_set_range() To change the value range
 * @see widget_destroy() To free the progress bar when done
 */
progressbar_t *progressbar_create(widget_t *parent) {
    progressbar_t *pb = (progressbar_t *)malloc(sizeof(progressbar_t));
    if (!pb)
        return NULL;

    memset(pb, 0, sizeof(progressbar_t));

    // Initialize base widget
    pb->base.type = WIDGET_PROGRESSBAR;
    pb->base.parent = parent;
    pb->base.visible = true;
    pb->base.enabled = true;
    pb->base.bg_color = WB_GRAY_LIGHT;
    pb->base.fg_color = WB_BLACK;
    pb->base.width = 200;
    pb->base.height = 20;

    // Set handlers
    pb->base.on_paint = progressbar_paint;

    // Default range
    pb->min_val = 0;
    pb->max_val = 100;
    pb->value = 0;
    pb->show_text = true;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)pb);
    }

    return pb;
}

/**
 * @brief Sets the current progress value.
 *
 * This function updates the progress bar's current value, which determines
 * how much of the bar is filled. The value is automatically clamped to the
 * valid range (min_val to max_val).
 *
 * The visual fill percentage is:
 * - 0% when value equals min_val
 * - 100% when value equals max_val
 * - Proportional for values in between
 *
 * @param pb    Pointer to the progress bar widget. If NULL, does nothing.
 * @param value The new progress value. Values outside the range are clamped
 *              to min_val or max_val as appropriate.
 *
 * @note This function does not trigger a repaint. Call widget_app_repaint()
 *       to see the visual change.
 *
 * @note For smooth progress updates, call this function frequently with
 *       incrementing values rather than just updating at task completion.
 *
 * @code
 * // Update progress during a loop
 * int total_items = 500;
 * for (int i = 0; i < total_items; i++) {
 *     progressbar_set_value(pb, (i * 100) / total_items);
 *     widget_app_repaint(app);
 *     process_item(i);
 * }
 * progressbar_set_value(pb, 100);  // Ensure 100% at end
 * @endcode
 *
 * @see progressbar_get_value() To retrieve the current value
 * @see progressbar_set_range() To change the value range
 */
void progressbar_set_value(progressbar_t *pb, int value) {
    if (!pb)
        return;

    if (value < pb->min_val)
        value = pb->min_val;
    if (value > pb->max_val)
        value = pb->max_val;

    pb->value = value;
}

/**
 * @brief Retrieves the current progress value.
 *
 * @param pb Pointer to the progress bar widget. If NULL, returns 0.
 *
 * @return The current progress value, or 0 if pb is NULL.
 *
 * @see progressbar_set_value() To change the current value
 */
int progressbar_get_value(progressbar_t *pb) {
    return pb ? pb->value : 0;
}

/**
 * @brief Sets the minimum and maximum value range for the progress bar.
 *
 * This function defines the range of values that the progress bar represents.
 * The visual fill is calculated based on where the current value falls within
 * this range. Common ranges include:
 * - 0-100: Standard percentage (default)
 * - 0-N: For N items to process
 * - 0-file_size: For byte-based progress
 *
 * If the current value is outside the new range, it is clamped to fit.
 *
 * @param pb      Pointer to the progress bar widget. If NULL, does nothing.
 * @param min_val The minimum value (represents 0% / empty).
 * @param max_val The maximum value (represents 100% / full).
 *
 * @note If min_val >= max_val, the progress bar will show 0% regardless
 *       of the current value (division by zero is avoided internally).
 *
 * @note This function does not trigger a repaint.
 *
 * @code
 * // Set up for file copy (byte-based progress)
 * long file_size = get_file_size(source);
 * progressbar_set_range(pb, 0, (int)file_size);
 *
 * // Update as bytes are copied
 * progressbar_set_value(pb, bytes_copied);
 * @endcode
 *
 * @see progressbar_set_value() To update the current value
 */
void progressbar_set_range(progressbar_t *pb, int min_val, int max_val) {
    if (!pb)
        return;

    pb->min_val = min_val;
    pb->max_val = max_val;

    // Clamp current value
    if (pb->value < min_val)
        pb->value = min_val;
    if (pb->value > max_val)
        pb->value = max_val;
}

/**
 * @brief Enables or disables the percentage text display.
 *
 * When enabled (the default), the progress bar displays a centered percentage
 * text like "45%" indicating the completion level. When disabled, only the
 * fill bar is shown without text.
 *
 * Reasons to disable text:
 * - Very small progress bars where text doesn't fit
 * - Visual preference for cleaner appearance
 * - When the percentage isn't meaningful (e.g., indeterminate progress)
 *
 * @param pb   Pointer to the progress bar widget. If NULL, does nothing.
 * @param show True to show percentage text, false to hide it.
 *
 * @note This function does not trigger a repaint.
 *
 * @see progressbar_paint() Where the text is rendered
 */
void progressbar_set_show_text(progressbar_t *pb, bool show) {
    if (pb) {
        pb->show_text = show;
    }
}
