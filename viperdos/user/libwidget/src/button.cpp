//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file button.c
 * @brief Push button widget implementation for the libwidget toolkit.
 *
 * This file implements a clickable push button widget that renders with
 * classic Amiga Workbench 3.x styling—a 3D beveled appearance that visually
 * depresses when clicked.
 *
 * ## Visual Style
 *
 * Buttons are rendered with a raised 3D appearance using the draw_3d_button()
 * helper function. When pressed, the button appears sunken (3D effect inverts)
 * and the text label shifts slightly down and right to enhance the pressed
 * appearance.
 *
 * ## Interaction Model
 *
 * Buttons respond to left mouse button clicks (button 0). The click handler:
 * 1. Sets the pressed state to true (for visual feedback)
 * 2. Immediately resets pressed to false
 * 3. Invokes the registered on_click callback if present
 *
 * This implementation triggers the callback on mouse-down. A more sophisticated
 * implementation would track mouse-up within the button bounds, but the current
 * approach provides immediate feedback that works well for the ViperDOS
 * environment.
 *
 * ## Default Dimensions
 *
 * Newly created buttons have a default size of 80x24 pixels, suitable for
 * short button labels. Use widget_set_size() to adjust for longer text or
 * different layout requirements.
 *
 * @see widget.h for the button_t structure definition
 * @see draw3d.c for the 3D rendering helpers
 */
//===----------------------------------------------------------------------===//

#include <stdlib.h>
#include <string.h>
#include <widget.h>

//===----------------------------------------------------------------------===//
// Button Paint Handler
//===----------------------------------------------------------------------===//

/**
 * @brief Renders a button widget with 3D beveled styling.
 *
 * This paint handler draws the button with classic Amiga Workbench 3.x
 * styling. The rendering process includes:
 *
 * 1. **3D Frame**: Draws a beveled button frame using draw_3d_button().
 *    The frame appears raised when unpressed and sunken when pressed,
 *    achieved by inverting the light/dark edge colors.
 *
 * 2. **Text Label**: Centers the button text both horizontally and vertically
 *    within the button bounds. Text is rendered in black when enabled or
 *    medium gray when disabled to indicate the non-interactive state.
 *
 * 3. **Press Offset**: When the button is pressed, the text is shifted
 *    1 pixel down and right to create the illusion of the button surface
 *    being pushed inward.
 *
 * The text centering uses the following calculations:
 * - Horizontal: (button_width - text_width) / 2, where text_width = length * 8
 * - Vertical: (button_height - font_height) / 2, where font_height = 10
 *
 * @param w   Pointer to the base widget structure. Cast to button_t* to access
 *            button-specific fields (text, pressed state).
 * @param win Pointer to the GUI window for drawing operations. Used with
 *            gui_draw_text() and draw_3d_button().
 *
 * @note The font is assumed to be 8 pixels wide per character and 10 pixels
 *       tall. These values are hardcoded for the ViperDOS bitmap font.
 *
 * @note This function is registered as the on_paint callback during button
 *       creation and is called automatically by widget_paint().
 *
 * @see draw_3d_button() For the beveled frame rendering
 * @see button_click() For the pressed state management
 */
static void button_paint(widget_t *w, gui_window_t *win) {
    button_t *btn = (button_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw 3D button
    draw_3d_button(win, x, y, width, height, btn->pressed);

    // Draw text centered
    int text_len = (int)strlen(btn->text);
    int text_width = text_len * 8;
    int text_x = x + (width - text_width) / 2;
    int text_y = y + (height - 10) / 2;

    // Offset text when pressed
    if (btn->pressed) {
        text_x++;
        text_y++;
    }

    uint32_t text_color = w->enabled ? WB_BLACK : WB_GRAY_MED;
    gui_draw_text(win, text_x, text_y, btn->text, text_color);
}

//===----------------------------------------------------------------------===//
// Button Event Handlers
//===----------------------------------------------------------------------===//

/**
 * @brief Handles mouse click events on the button.
 *
 * This internal click handler is invoked when a mouse button is pressed
 * while the cursor is within the button's bounds. It implements the
 * following behavior:
 *
 * 1. **Button Filter**: Only responds to left mouse button (button 0).
 *    Right-click and middle-click are ignored.
 *
 * 2. **Visual Feedback**: Sets the pressed state to true momentarily.
 *    In the current implementation, this is immediately reset to false
 *    before the callback is invoked.
 *
 * 3. **Callback Invocation**: If a click callback was registered via
 *    button_set_onclick(), it is called with the user-provided data.
 *
 * The current implementation triggers the action on mouse-down rather than
 * mouse-up. This provides immediate feedback but differs from some GUI
 * toolkits that require the mouse-up to occur within the button bounds.
 *
 * @param w      Pointer to the base widget structure (cast to button_t internally).
 * @param x      X coordinate of the click in widget-local space (unused).
 * @param y      Y coordinate of the click in widget-local space (unused).
 * @param button Mouse button identifier: 0=left, 1=middle, 2=right.
 *               Only button 0 (left click) triggers the action.
 *
 * @note The x and y parameters are provided but unused. They could be used
 *       for buttons with multiple clickable regions if needed in the future.
 *
 * @note This function is registered as the base widget's on_click callback
 *       during button_create() and is called by widget_handle_mouse().
 *
 * @see button_set_onclick() To register a callback for button clicks
 * @see widget_handle_mouse() The event dispatch function that calls this
 */
static void button_click(widget_t *w, int x, int y, int button) {
    (void)x;
    (void)y;

    if (button != 0)
        return;

    button_t *btn = (button_t *)w;
    btn->pressed = true;

    // The release will trigger the callback
    // For now, trigger immediately
    btn->pressed = false;

    if (btn->on_click) {
        btn->on_click(btn->callback_data);
    }
}

//===----------------------------------------------------------------------===//
// Button API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new push button widget.
 *
 * This function allocates and initializes a button widget with the specified
 * text label. The button is automatically added to the parent widget's
 * children list if a parent is provided.
 *
 * The created button has the following default properties:
 * - **Size**: 80x24 pixels (suitable for short labels)
 * - **Position**: (0, 0) - caller should use widget_set_position() to place it
 * - **Colors**: Light gray background, black text
 * - **State**: Visible, enabled, not pressed
 * - **Callback**: None (set via button_set_onclick())
 *
 * ## Memory Management
 *
 * The button structure is allocated on the heap and must be freed when no
 * longer needed. If the button has a parent, destroying the parent will
 * automatically destroy the button. Otherwise, call widget_destroy() on
 * the button's base widget.
 *
 * ## Example Usage
 *
 * @code
 * // Create a button
 * button_t *ok_btn = button_create(dialog, "OK");
 * widget_set_position((widget_t *)ok_btn, 100, 200);
 * widget_set_size((widget_t *)ok_btn, 60, 24);
 *
 * // Register click handler
 * button_set_onclick(ok_btn, on_ok_clicked, dialog_context);
 * @endcode
 *
 * @param parent Pointer to the parent widget container. If non-NULL, the
 *               button is added to this parent's child list. Pass NULL
 *               for buttons that will be added to a parent later or that
 *               are managed independently.
 * @param text   The label text to display on the button. The text is copied
 *               into an internal buffer (maximum 63 characters). Pass NULL
 *               or empty string for a blank button.
 *
 * @return Pointer to the newly created button, or NULL if memory allocation
 *         failed. The returned pointer can be cast to widget_t* for use
 *         with generic widget functions.
 *
 * @see button_set_text() To change the button label after creation
 * @see button_set_onclick() To register a click handler
 * @see widget_destroy() To free the button when done
 */
button_t *button_create(widget_t *parent, const char *text) {
    button_t *btn = (button_t *)malloc(sizeof(button_t));
    if (!btn)
        return NULL;

    memset(btn, 0, sizeof(button_t));

    // Initialize base widget
    btn->base.type = WIDGET_BUTTON;
    btn->base.parent = parent;
    btn->base.visible = true;
    btn->base.enabled = true;
    btn->base.bg_color = WB_GRAY_LIGHT;
    btn->base.fg_color = WB_BLACK;
    btn->base.width = 80;
    btn->base.height = 24;

    // Set handlers
    btn->base.on_paint = button_paint;
    btn->base.on_click = button_click;

    // Set text
    if (text) {
        strncpy(btn->text, text, sizeof(btn->text) - 1);
        btn->text[sizeof(btn->text) - 1] = '\0';
    }

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)btn);
    }

    return btn;
}

/**
 * @brief Changes the text label displayed on a button.
 *
 * This function updates the button's label text. The new text is copied
 * into the button's internal buffer, which has a maximum capacity of
 * 63 characters (plus null terminator).
 *
 * After calling this function, you should trigger a repaint to see the
 * change (e.g., via widget_app_repaint()).
 *
 * @param btn  Pointer to the button widget. If NULL, does nothing.
 * @param text The new label text. If NULL, does nothing. Text longer than
 *             63 characters is truncated.
 *
 * @note The button is not automatically resized to fit the new text.
 *       If the new text is longer than the button width can display,
 *       it will be clipped. Use widget_set_size() to adjust the button
 *       width if needed.
 *
 * @see button_get_text() To retrieve the current button text
 * @see button_create() Which sets the initial text
 */
void button_set_text(button_t *btn, const char *text) {
    if (btn && text) {
        strncpy(btn->text, text, sizeof(btn->text) - 1);
        btn->text[sizeof(btn->text) - 1] = '\0';
    }
}

/**
 * @brief Retrieves the current text label of a button.
 *
 * This function returns a pointer to the button's internal text buffer.
 * The returned string is owned by the button and should not be modified
 * or freed by the caller.
 *
 * @param btn Pointer to the button widget. If NULL, returns NULL.
 *
 * @return Pointer to the button's text string, or NULL if btn is NULL.
 *         The returned pointer remains valid until the button is destroyed
 *         or button_set_text() is called.
 *
 * @see button_set_text() To change the button text
 */
const char *button_get_text(button_t *btn) {
    return btn ? btn->text : NULL;
}

/**
 * @brief Registers a callback function for button click events.
 *
 * This function sets up a callback that will be invoked whenever the user
 * clicks the button with the left mouse button. The callback receives a
 * single user-defined data pointer that can be used to pass context
 * information.
 *
 * Only one callback can be registered at a time. Calling this function
 * again replaces any previously registered callback. To remove a callback,
 * pass NULL for the callback parameter.
 *
 * ## Callback Signature
 *
 * The callback function must match the widget_callback_fn signature:
 * @code
 * void my_callback(void *data);
 * @endcode
 *
 * The `data` parameter is whatever was passed to button_set_onclick().
 *
 * ## Example Usage
 *
 * @code
 * // Callback function
 * static void on_submit_clicked(void *data) {
 *     my_form_t *form = (my_form_t *)data;
 *     process_form(form);
 * }
 *
 * // Registration
 * button_set_onclick(submit_btn, on_submit_clicked, form);
 * @endcode
 *
 * @param btn      Pointer to the button widget. If NULL, does nothing.
 * @param callback The function to call when the button is clicked, or NULL
 *                 to remove any existing callback.
 * @param data     User-defined data passed to the callback function. This
 *                 pointer is stored and passed unchanged to the callback.
 *
 * @note The callback is invoked on mouse button-down, not mouse button-up.
 *       This provides immediate feedback but differs from some GUI toolkits.
 *
 * @note The button does NOT take ownership of the data pointer. The caller
 *       is responsible for ensuring the data remains valid as long as the
 *       button exists and might invoke the callback.
 *
 * @see button_click() The internal handler that invokes this callback
 */
void button_set_onclick(button_t *btn, widget_callback_fn callback, void *data) {
    if (btn) {
        btn->on_click = callback;
        btn->callback_data = data;
    }
}
