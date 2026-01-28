//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file label.c
 * @brief Static text label widget implementation for the libwidget toolkit.
 *
 * This file implements a non-interactive text label widget used to display
 * static text in the user interface. Labels are commonly used for:
 * - Form field descriptions
 * - Status messages
 * - Dialog instructions
 * - Section headings
 *
 * ## Visual Characteristics
 *
 * Labels are rendered as plain text without any border or background fill.
 * The text is drawn directly using the widget's foreground color against
 * whatever background exists behind the label (typically the parent
 * container's background).
 *
 * ## Text Alignment
 *
 * Labels support three alignment modes:
 * - **ALIGN_LEFT** (default): Text starts at the left edge of the widget
 * - **ALIGN_CENTER**: Text is horizontally centered within the widget bounds
 * - **ALIGN_RIGHT**: Text ends at the right edge of the widget
 *
 * Vertical alignment is always centered within the widget height.
 *
 * ## Non-Interactive
 *
 * Labels do not respond to mouse clicks or keyboard input. They are purely
 * for display purposes. If you need clickable text, consider using a button
 * with minimal styling or implementing a custom widget.
 *
 * @see widget.h for the label_t structure definition
 */
//===----------------------------------------------------------------------===//

#include <stdlib.h>
#include <string.h>
#include <widget.h>

//===----------------------------------------------------------------------===//
// Label Paint Handler
//===----------------------------------------------------------------------===//

/**
 * @brief Renders a label widget by drawing its text with proper alignment.
 *
 * This paint handler draws the label's text using the configured alignment
 * mode. The text is rendered in the widget's foreground color with no
 * background fill (transparent background).
 *
 * ## Alignment Calculation
 *
 * The horizontal position of the text depends on the alignment setting:
 * - **ALIGN_LEFT**: text_x = widget.x (start at left edge)
 * - **ALIGN_CENTER**: text_x = widget.x + (width - text_width) / 2
 * - **ALIGN_RIGHT**: text_x = widget.x + width - text_width
 *
 * The vertical position centers the text within the widget height:
 * - text_y = widget.y + (height - font_height) / 2
 *
 * where font_height is assumed to be 10 pixels.
 *
 * @param w   Pointer to the base widget structure. Cast to label_t* to access
 *            label-specific fields (text, alignment).
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note The text width is calculated as (string_length * 8) pixels,
 *       assuming the ViperDOS fixed-width font at 8 pixels per character.
 *
 * @note Labels don't draw a background. They rely on the parent container's
 *       background color showing through. If you need a background, place
 *       the label in a container with a specific background color.
 *
 * @note This function is registered as the on_paint callback during
 *       label_create() and is called automatically by widget_paint().
 *
 * @see label_set_alignment() To change the alignment mode
 */
static void label_paint(widget_t *w, gui_window_t *win) {
    label_t *lbl = (label_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;

    int text_len = (int)strlen(lbl->text);
    int text_width = text_len * 8;
    int text_x;

    switch (lbl->alignment) {
        case ALIGN_CENTER:
            text_x = x + (width - text_width) / 2;
            break;
        case ALIGN_RIGHT:
            text_x = x + width - text_width;
            break;
        case ALIGN_LEFT:
        default:
            text_x = x;
            break;
    }

    int text_y = y + (w->height - 10) / 2;

    gui_draw_text(win, text_x, text_y, lbl->text, w->fg_color);
}

//===----------------------------------------------------------------------===//
// Label API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new static text label widget.
 *
 * This function allocates and initializes a label widget with the specified
 * text content. Labels are non-interactive widgets designed for displaying
 * static text information.
 *
 * The created label has the following default properties:
 * - **Size**: 100x16 pixels (suitable for single-line text)
 * - **Position**: (0, 0) - caller should use widget_set_position() to place it
 * - **Colors**: Black text on light gray background (transparent)
 * - **Alignment**: ALIGN_LEFT (text starts at left edge)
 * - **State**: Visible, enabled (though labels don't process input)
 *
 * ## Example Usage
 *
 * @code
 * // Create a form label
 * label_t *name_label = label_create(form_panel, "Name:");
 * widget_set_position((widget_t *)name_label, 10, 30);
 *
 * // Create a centered title
 * label_t *title = label_create(dialog, "Settings");
 * widget_set_position((widget_t *)title, 0, 10);
 * widget_set_size((widget_t *)title, dialog_width, 20);
 * label_set_alignment(title, ALIGN_CENTER);
 * @endcode
 *
 * @param parent Pointer to the parent widget container. If non-NULL, the
 *               label is added to this parent's child list. Pass NULL
 *               for labels that will be added to a parent later.
 * @param text   The text string to display. The text is copied into an
 *               internal buffer (maximum 127 characters). Pass NULL or
 *               empty string for a blank label that can be set later.
 *
 * @return Pointer to the newly created label, or NULL if memory allocation
 *         failed. The returned pointer can be cast to widget_t* for use
 *         with generic widget functions.
 *
 * @note Labels do not register click or key handlers since they are
 *       non-interactive. They only have a paint handler.
 *
 * @see label_set_text() To change the label text after creation
 * @see label_set_alignment() To change text alignment
 * @see widget_destroy() To free the label when done
 */
label_t *label_create(widget_t *parent, const char *text) {
    label_t *lbl = (label_t *)malloc(sizeof(label_t));
    if (!lbl)
        return NULL;

    memset(lbl, 0, sizeof(label_t));

    // Initialize base widget
    lbl->base.type = WIDGET_LABEL;
    lbl->base.parent = parent;
    lbl->base.visible = true;
    lbl->base.enabled = true;
    lbl->base.bg_color = WB_GRAY_LIGHT;
    lbl->base.fg_color = WB_BLACK;
    lbl->base.width = 100;
    lbl->base.height = 16;

    // Set handlers
    lbl->base.on_paint = label_paint;

    // Set text
    if (text) {
        strncpy(lbl->text, text, sizeof(lbl->text) - 1);
        lbl->text[sizeof(lbl->text) - 1] = '\0';
    }

    lbl->alignment = ALIGN_LEFT;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)lbl);
    }

    return lbl;
}

/**
 * @brief Changes the text displayed by a label widget.
 *
 * This function updates the label's text content. The new text is copied
 * into the label's internal buffer, which has a maximum capacity of
 * 127 characters (plus null terminator).
 *
 * After calling this function, you should trigger a repaint to see the
 * change (e.g., via widget_app_repaint()).
 *
 * @param lbl  Pointer to the label widget. If NULL, does nothing.
 * @param text The new text to display. If NULL, does nothing. Text longer
 *             than 127 characters is truncated.
 *
 * @note The label is not automatically resized to fit the new text.
 *       If the text is longer than the widget width allows, it may be
 *       clipped or extend beyond the widget bounds depending on alignment.
 *
 * @note For dynamically updating content (like a status bar), consider
 *       keeping the label wide enough for the longest expected text.
 *
 * @see label_get_text() To retrieve the current text
 * @see label_create() Which sets the initial text
 */
void label_set_text(label_t *lbl, const char *text) {
    if (lbl && text) {
        strncpy(lbl->text, text, sizeof(lbl->text) - 1);
        lbl->text[sizeof(lbl->text) - 1] = '\0';
    }
}

/**
 * @brief Retrieves the current text content of a label.
 *
 * This function returns a pointer to the label's internal text buffer.
 * The returned string is owned by the label and should not be modified
 * or freed by the caller.
 *
 * @param lbl Pointer to the label widget. If NULL, returns NULL.
 *
 * @return Pointer to the label's text string, or NULL if lbl is NULL.
 *         The returned pointer remains valid until the label is destroyed
 *         or label_set_text() is called with a new value.
 *
 * @see label_set_text() To change the label text
 */
const char *label_get_text(label_t *lbl) {
    return lbl ? lbl->text : NULL;
}

/**
 * @brief Sets the text alignment mode for a label.
 *
 * This function controls how the label's text is positioned horizontally
 * within the widget's bounds. The alignment affects only the horizontal
 * positioning; vertical positioning is always centered.
 *
 * Available alignment modes:
 * - **ALIGN_LEFT**: Text starts at the left edge of the widget. This is
 *   the default and most common choice for form labels and paragraphs.
 * - **ALIGN_CENTER**: Text is centered horizontally. Useful for titles,
 *   headings, and centered dialog text.
 * - **ALIGN_RIGHT**: Text ends at the right edge of the widget. Useful
 *   for numeric values in tables or right-aligned form labels.
 *
 * After changing alignment, trigger a repaint to see the change.
 *
 * @param lbl   Pointer to the label widget. If NULL, does nothing.
 * @param align The new alignment mode (ALIGN_LEFT, ALIGN_CENTER, or ALIGN_RIGHT).
 *
 * @note Alignment only affects rendering; it does not change the widget's
 *       position or size. The widget bounds remain the same.
 *
 * @code
 * // Create a right-aligned number display
 * label_t *value_label = label_create(panel, "1234");
 * widget_set_size((widget_t *)value_label, 80, 16);
 * label_set_alignment(value_label, ALIGN_RIGHT);
 * @endcode
 *
 * @see label_paint() Where alignment is applied during rendering
 */
void label_set_alignment(label_t *lbl, alignment_t align) {
    if (lbl) {
        lbl->alignment = align;
    }
}
