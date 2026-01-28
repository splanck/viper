//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file checkbox.c
 * @brief Checkbox toggle widget implementation for the libwidget toolkit.
 *
 * This file implements a checkbox widget that displays a toggleable boolean
 * option with an associated text label. Checkboxes are commonly used for:
 * - Enabling or disabling features
 * - Yes/no or true/false settings
 * - Multiple independent selections (unlike radio buttons)
 *
 * ## Visual Design
 *
 * The checkbox consists of two parts:
 * 1. **Check Box**: A 14x14 pixel sunken square that can contain a checkmark
 * 2. **Label Text**: A text string displayed to the right of the box
 *
 * When checked, a "V" shaped checkmark is drawn inside the box using two
 * thick diagonal lines. The checkmark is rendered in black when enabled
 * or medium gray when disabled.
 *
 * ## Interaction Model
 *
 * Clicking anywhere on the checkbox widget (box or label) toggles the
 * checked state. Each click inverts the current state and triggers the
 * on_change callback if registered.
 *
 * The checkbox can also be controlled programmatically via checkbox_set_checked()
 * without triggering the callback.
 *
 * ## Layout
 *
 * The checkbox box is vertically centered within the widget height. The label
 * text is drawn 6 pixels to the right of the checkbox and also vertically
 * centered. The default widget size is 150x20 pixels, suitable for short labels.
 *
 * @see widget.h for the checkbox_t structure definition
 */
//===----------------------------------------------------------------------===//

#include <stdlib.h>
#include <string.h>
#include <widget.h>

/**
 * @brief Size of the checkbox square in pixels (14x14).
 *
 * This constant defines both the width and height of the checkable box area.
 * The box contains a 2-pixel sunken border, leaving 10x10 pixels for the
 * checkmark when checked.
 */
#define CHECKBOX_SIZE 14

//===----------------------------------------------------------------------===//
// Checkbox Paint Handler
//===----------------------------------------------------------------------===//

/**
 * @brief Renders the checkbox widget with box, checkmark, and label.
 *
 * This paint handler draws the complete checkbox visual representation:
 *
 * 1. **Sunken Box**: A 14x14 pixel sunken frame indicating the toggleable
 *    area, drawn using draw_3d_sunken() for the classic inset appearance.
 *
 * 2. **White Background**: The interior of the box is filled with white
 *    to provide a clean background for the checkmark.
 *
 * 3. **Checkmark** (if checked): A "V" shaped mark drawn as two diagonal
 *    lines meeting at the bottom. The checkmark is rendered twice (offset
 *    by 1 pixel) to create a thick, visible mark.
 *
 * 4. **Label Text**: The checkbox label is drawn 6 pixels to the right of
 *    the box, vertically centered within the widget.
 *
 * ## Checkmark Drawing
 *
 * The checkmark is composed of:
 * - A short descending line from top-left (3 pixels long)
 * - A longer ascending line to top-right (5 pixels long)
 * - Both lines are drawn twice (offset by 1 pixel) for thickness
 *
 * @param w   Pointer to the base widget structure (cast to checkbox_t internally).
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note The checkbox box is vertically centered within the widget height,
 *       calculated as: box_y = y + (height - CHECKBOX_SIZE) / 2
 *
 * @note Disabled checkboxes show the checkmark in medium gray (WB_GRAY_MED)
 *       instead of black to indicate the non-interactive state.
 *
 * @see draw_3d_sunken() For the box frame rendering
 */
static void checkbox_paint(widget_t *w, gui_window_t *win) {
    checkbox_t *cb = (checkbox_t *)w;

    int x = w->x;
    int y = w->y;
    int box_y = y + (w->height - CHECKBOX_SIZE) / 2;

    // Draw checkbox box (sunken)
    draw_3d_sunken(win, x, box_y, CHECKBOX_SIZE, CHECKBOX_SIZE, WB_WHITE, WB_WHITE, WB_GRAY_DARK);

    // Fill background
    gui_fill_rect(win, x + 2, box_y + 2, CHECKBOX_SIZE - 4, CHECKBOX_SIZE - 4, WB_WHITE);

    // Draw checkmark if checked
    if (cb->checked) {
        uint32_t check_color = w->enabled ? WB_BLACK : WB_GRAY_MED;

        // Simple checkmark (two lines)
        int cx = x + 3;
        int cy = box_y + 3;

        // Draw checkmark as two lines forming a "V" rotated
        for (int i = 0; i < 3; i++) {
            gui_fill_rect(win, cx + i, cy + i + 3, 1, 1, check_color);
        }
        for (int i = 0; i < 5; i++) {
            gui_fill_rect(win, cx + 3 + i, cy + 5 - i, 1, 1, check_color);
        }

        // Make checkmark thicker
        for (int i = 0; i < 3; i++) {
            gui_fill_rect(win, cx + i, cy + i + 4, 1, 1, check_color);
        }
        for (int i = 0; i < 5; i++) {
            gui_fill_rect(win, cx + 3 + i, cy + 6 - i, 1, 1, check_color);
        }
    }

    // Draw label
    int text_x = x + CHECKBOX_SIZE + 6;
    int text_y = y + (w->height - 10) / 2;

    uint32_t text_color = w->enabled ? WB_BLACK : WB_GRAY_MED;
    gui_draw_text(win, text_x, text_y, cb->text, text_color);
}

//===----------------------------------------------------------------------===//
// Checkbox Event Handlers
//===----------------------------------------------------------------------===//

/**
 * @brief Handles mouse click events on the checkbox.
 *
 * When the checkbox is clicked with the left mouse button, this handler:
 * 1. Toggles the checked state (checked becomes unchecked, and vice versa)
 * 2. Invokes the on_change callback if one is registered
 *
 * The click is handled regardless of whether it lands on the box or the
 * label area, making the entire widget clickable for ease of use.
 *
 * @param w      Pointer to the base widget structure (cast to checkbox_t internally).
 * @param x      X coordinate of the click in widget-local space (unused).
 * @param y      Y coordinate of the click in widget-local space (unused).
 * @param button Mouse button identifier. Only left click (button 0) toggles
 *               the checkbox; other buttons are ignored.
 *
 * @note The callback is invoked after the state change, so callback code
 *       can query checkbox_is_checked() to get the new state.
 *
 * @see checkbox_set_onchange() To register the change callback
 */
static void checkbox_click(widget_t *w, int x, int y, int button) {
    (void)x;
    (void)y;

    if (button != 0)
        return;

    checkbox_t *cb = (checkbox_t *)w;
    cb->checked = !cb->checked;

    if (cb->on_change) {
        cb->on_change(cb->callback_data);
    }
}

//===----------------------------------------------------------------------===//
// Checkbox API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new checkbox widget with a text label.
 *
 * This function allocates and initializes a checkbox widget. The checkbox
 * is initially unchecked and is ready for user interaction once added
 * to the widget tree.
 *
 * The created checkbox has the following default properties:
 * - **Size**: 150x20 pixels (box + space for label text)
 * - **Position**: (0, 0) - caller should use widget_set_position() to place it
 * - **Colors**: Light gray background, black text and checkmark
 * - **State**: Visible, enabled, unchecked
 * - **Callback**: None (set via checkbox_set_onchange())
 *
 * ## Example Usage
 *
 * @code
 * // Create a settings panel with checkboxes
 * checkbox_t *sound_cb = checkbox_create(settings, "Enable Sound");
 * widget_set_position((widget_t *)sound_cb, 20, 50);
 * checkbox_set_checked(sound_cb, true);  // Default to checked
 * checkbox_set_onchange(sound_cb, on_sound_toggle, &settings);
 *
 * checkbox_t *music_cb = checkbox_create(settings, "Enable Music");
 * widget_set_position((widget_t *)music_cb, 20, 80);
 * @endcode
 *
 * @param parent Pointer to the parent widget container. If non-NULL, the
 *               checkbox is added to this parent's child list. Pass NULL
 *               for checkboxes that will be added to a parent later.
 * @param text   The label text displayed next to the checkbox. The text is
 *               copied into an internal buffer (maximum 63 characters).
 *               Pass NULL or empty string for a checkbox without a label.
 *
 * @return Pointer to the newly created checkbox, or NULL if memory allocation
 *         failed. The returned pointer can be cast to widget_t* for use
 *         with generic widget functions.
 *
 * @see checkbox_set_checked() To set the initial checked state
 * @see checkbox_set_onchange() To register a state change callback
 * @see widget_destroy() To free the checkbox when done
 */
checkbox_t *checkbox_create(widget_t *parent, const char *text) {
    checkbox_t *cb = (checkbox_t *)malloc(sizeof(checkbox_t));
    if (!cb)
        return NULL;

    memset(cb, 0, sizeof(checkbox_t));

    // Initialize base widget
    cb->base.type = WIDGET_CHECKBOX;
    cb->base.parent = parent;
    cb->base.visible = true;
    cb->base.enabled = true;
    cb->base.bg_color = WB_GRAY_LIGHT;
    cb->base.fg_color = WB_BLACK;
    cb->base.width = 150;
    cb->base.height = 20;

    // Set handlers
    cb->base.on_paint = checkbox_paint;
    cb->base.on_click = checkbox_click;

    // Set text
    if (text) {
        strncpy(cb->text, text, sizeof(cb->text) - 1);
        cb->text[sizeof(cb->text) - 1] = '\0';
    }

    cb->checked = false;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)cb);
    }

    return cb;
}

/**
 * @brief Changes the label text displayed next to a checkbox.
 *
 * This function updates the checkbox's label. The new text is copied into
 * the internal buffer, which has a maximum capacity of 63 characters.
 *
 * @param cb   Pointer to the checkbox widget. If NULL, does nothing.
 * @param text The new label text. If NULL, does nothing. Text longer than
 *             63 characters is truncated.
 *
 * @note The checkbox is not automatically resized to fit the new text.
 *       If the new label is longer, use widget_set_size() to ensure the
 *       text is fully visible.
 *
 * @note Trigger a repaint to see the label change.
 *
 * @see checkbox_create() Which sets the initial label
 */
void checkbox_set_text(checkbox_t *cb, const char *text) {
    if (cb && text) {
        strncpy(cb->text, text, sizeof(cb->text) - 1);
        cb->text[sizeof(cb->text) - 1] = '\0';
    }
}

/**
 * @brief Programmatically sets the checked state of a checkbox.
 *
 * This function changes the checkbox's checked state without triggering
 * the on_change callback. This is useful for:
 * - Setting an initial state based on saved preferences
 * - Synchronizing checkbox state with external data
 * - Implementing "check all" / "uncheck all" functionality
 *
 * @param cb      Pointer to the checkbox widget. If NULL, does nothing.
 * @param checked True to check the checkbox, false to uncheck it.
 *
 * @note Unlike user clicks, this function does NOT invoke the on_change
 *       callback. If you need callback behavior, invoke it manually after
 *       this call.
 *
 * @note Trigger a repaint to see the visual change.
 *
 * @see checkbox_is_checked() To query the current state
 * @see checkbox_click() Which toggles state AND calls the callback
 */
void checkbox_set_checked(checkbox_t *cb, bool checked) {
    if (cb) {
        cb->checked = checked;
    }
}

/**
 * @brief Queries whether a checkbox is currently checked.
 *
 * @param cb Pointer to the checkbox widget. If NULL, returns false.
 *
 * @return True if the checkbox is checked, false if unchecked or if cb is NULL.
 *
 * @see checkbox_set_checked() To change the checked state
 */
bool checkbox_is_checked(checkbox_t *cb) {
    return cb ? cb->checked : false;
}

/**
 * @brief Registers a callback for checkbox state changes.
 *
 * The on_change callback is invoked whenever the user clicks the checkbox
 * to toggle its state. The callback is called AFTER the state change, so
 * you can query checkbox_is_checked() to get the new state.
 *
 * @param cb       Pointer to the checkbox widget. If NULL, does nothing.
 * @param callback The function to call when the checkbox is toggled, or NULL
 *                 to remove any existing callback.
 * @param data     User-defined data passed to the callback function.
 *
 * @note The callback is NOT invoked for programmatic changes via
 *       checkbox_set_checked(). Only user interaction triggers the callback.
 *
 * ## Example Usage
 *
 * @code
 * static void on_setting_changed(void *data) {
 *     settings_t *settings = (settings_t *)data;
 *     checkbox_t *cb = settings->current_checkbox;
 *
 *     if (checkbox_is_checked(cb)) {
 *         printf("Setting enabled\n");
 *     } else {
 *         printf("Setting disabled\n");
 *     }
 * }
 *
 * checkbox_set_onchange(cb, on_setting_changed, settings);
 * @endcode
 *
 * @see checkbox_click() Which invokes this callback on toggle
 */
void checkbox_set_onchange(checkbox_t *cb, widget_callback_fn callback, void *data) {
    if (cb) {
        cb->on_change = callback;
        cb->callback_data = data;
    }
}
