//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file textbox.c
 * @brief Single-line text input widget implementation for the libwidget toolkit.
 *
 * This file implements an editable text input field with support for:
 * - Cursor positioning and navigation (arrow keys, Home, End)
 * - Text insertion and deletion (typing, Backspace, Delete)
 * - Text selection and selection deletion
 * - Password mode (display asterisks instead of actual characters)
 * - Read-only mode (display text without allowing edits)
 * - Horizontal scrolling for text longer than the visible area
 * - Callbacks for text changes and Enter key press
 *
 * ## Visual Design
 *
 * The textbox is rendered with a sunken 3D frame to indicate that it is
 * an input field (vs. a raised button or flat label). The interior is
 * filled with white for good text legibility, and a vertical line cursor
 * indicates the insertion point.
 *
 * ## Text Buffer Management
 *
 * The textbox uses a dynamically allocated text buffer that grows as needed.
 * Initial capacity is 256 bytes, doubling when more space is required. The
 * buffer is always null-terminated.
 *
 * ## Keyboard Navigation
 *
 * The following keys are handled:
 * - **Left/Right Arrow**: Move cursor one character
 * - **Home**: Move cursor to start of text
 * - **End**: Move cursor to end of text
 * - **Backspace**: Delete character before cursor, or delete selection
 * - **Delete**: Delete character after cursor, or delete selection
 * - **Enter**: Trigger on_enter callback (does not insert newline)
 * - **Printable characters (32-126)**: Insert at cursor position
 *
 * ## Selection Support
 *
 * Text selection is tracked via selection_start and selection_end indices.
 * When these differ, the selected region is highlighted with a blue background
 * and white text. Typing while text is selected replaces the selection.
 *
 * @see widget.h for the textbox_t structure definition
 */
//===----------------------------------------------------------------------===//

#include <stdlib.h>
#include <string.h>
#include <widget.h>

/** @brief Initial buffer capacity for text storage (256 bytes). */
#define TEXTBOX_INITIAL_CAPACITY 256

/** @brief Width of each character in the fixed-width font (8 pixels). */
#define CHAR_WIDTH 8

/** @brief Height of each character in the fixed-width font (10 pixels). */
#define CHAR_HEIGHT 10

//===----------------------------------------------------------------------===//
// TextBox Paint Handler
//===----------------------------------------------------------------------===//

/**
 * @brief Renders the textbox widget with frame, text, cursor, and selection.
 *
 * This paint handler draws the complete textbox visual representation:
 *
 * 1. **Sunken Frame**: A 3D sunken border indicating an editable field,
 *    rendered using draw_3d_sunken() with dark edges to create the
 *    "cut into the surface" appearance.
 *
 * 2. **White Background**: The interior is filled with white (minus the
 *    2-pixel border on each side) for optimal text readability.
 *
 * 3. **Text Content**: The visible portion of the text is drawn, taking
 *    into account the scroll_offset for text longer than the visible area.
 *    In password mode, asterisks (*) are displayed instead of actual characters.
 *
 * 4. **Cursor**: When the textbox has focus and is editable, a vertical
 *    line cursor is drawn at the current cursor_pos.
 *
 * 5. **Selection Highlight**: If text is selected (selection_start !=
 *    selection_end), the selected region is highlighted with a blue
 *    background and the text redrawn in white for contrast.
 *
 * @param w   Pointer to the base widget structure (cast to textbox_t internally).
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note Text is clipped to the visible area. Horizontal scrolling is handled
 *       via scroll_offset, which is automatically adjusted as the cursor moves.
 *
 * @note The visible character count is calculated as (width - 8) / CHAR_WIDTH,
 *       leaving 4 pixels of padding on each side.
 *
 * @see textbox_ensure_cursor_visible() For scroll offset management
 * @see draw_3d_sunken() For the frame rendering
 */
static void textbox_paint(widget_t *w, gui_window_t *win) {
    textbox_t *tb = (textbox_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw sunken frame
    draw_3d_sunken(win, x, y, width, height, WB_WHITE, WB_WHITE, WB_GRAY_DARK);

    // Background
    gui_fill_rect(win, x + 2, y + 2, width - 4, height - 4, WB_WHITE);

    // Calculate visible text area
    int text_x = x + 4;
    int text_y = y + (height - CHAR_HEIGHT) / 2;
    int visible_chars = (width - 8) / CHAR_WIDTH;

    // Get text to display
    const char *display_text = tb->text ? tb->text : "";
    int text_len = tb->text_length;

    // Handle scroll offset
    int start = tb->scroll_offset;
    if (start > text_len)
        start = text_len;

    // Create display buffer
    char display_buf[256];
    int copy_len = text_len - start;
    if (copy_len > visible_chars)
        copy_len = visible_chars;
    if (copy_len > (int)sizeof(display_buf) - 1)
        copy_len = sizeof(display_buf) - 1;

    if (tb->password_mode) {
        // Show asterisks
        for (int i = 0; i < copy_len; i++) {
            display_buf[i] = '*';
        }
    } else {
        memcpy(display_buf, display_text + start, copy_len);
    }
    display_buf[copy_len] = '\0';

    // Draw text
    uint32_t text_color = w->enabled ? WB_BLACK : WB_GRAY_MED;
    gui_draw_text(win, text_x, text_y, display_buf, text_color);

    // Draw cursor if focused
    if (w->focused && w->enabled && !tb->readonly) {
        int cursor_screen_pos = tb->cursor_pos - tb->scroll_offset;
        if (cursor_screen_pos >= 0 && cursor_screen_pos <= visible_chars) {
            int cursor_x = text_x + cursor_screen_pos * CHAR_WIDTH;
            gui_draw_vline(win, cursor_x, text_y, text_y + CHAR_HEIGHT, WB_BLACK);
        }
    }

    // Draw selection highlight
    if (tb->selection_start != tb->selection_end) {
        int sel_start =
            tb->selection_start < tb->selection_end ? tb->selection_start : tb->selection_end;
        int sel_end =
            tb->selection_start < tb->selection_end ? tb->selection_end : tb->selection_start;

        sel_start -= tb->scroll_offset;
        sel_end -= tb->scroll_offset;

        if (sel_start < 0)
            sel_start = 0;
        if (sel_end > visible_chars)
            sel_end = visible_chars;

        if (sel_start < sel_end) {
            int sel_x = text_x + sel_start * CHAR_WIDTH;
            int sel_width = (sel_end - sel_start) * CHAR_WIDTH;
            gui_fill_rect(win, sel_x, text_y, sel_width, CHAR_HEIGHT, WB_BLUE);

            // Redraw selected text in white
            char sel_buf[256];
            int sel_len = sel_end - sel_start;
            if (sel_len > (int)sizeof(sel_buf) - 1)
                sel_len = sizeof(sel_buf) - 1;
            memcpy(sel_buf, display_buf + sel_start, sel_len);
            sel_buf[sel_len] = '\0';
            gui_draw_text(win, sel_x, text_y, sel_buf, WB_WHITE);
        }
    }
}

//===----------------------------------------------------------------------===//
// TextBox Event Handlers
//===----------------------------------------------------------------------===//

/**
 * @brief Handles mouse click events on the textbox.
 *
 * When the textbox is clicked, this handler:
 * 1. Gives keyboard focus to the textbox
 * 2. Calculates which character position was clicked
 * 3. Moves the cursor to that position
 * 4. Clears any existing text selection
 *
 * The cursor position is calculated from the click's x-coordinate relative
 * to the text area, taking into account the scroll offset for long text.
 *
 * @param w      Pointer to the base widget structure (cast to textbox_t internally).
 * @param x      X coordinate of the click in widget-local space (relative to
 *               the textbox's left edge).
 * @param y      Y coordinate of the click (unused, as textbox is single-line).
 * @param button Mouse button identifier. Only left click (button 0) is handled.
 *
 * @note Click-and-drag selection is not currently implemented. Each click
 *       clears the selection and repositions the cursor.
 *
 * @see widget_set_focus() For the focus acquisition
 */
static void textbox_click(widget_t *w, int x, int y, int button) {
    (void)y;

    if (button != 0)
        return;

    textbox_t *tb = (textbox_t *)w;

    // Set focus
    widget_set_focus(w);

    // Calculate cursor position from click
    int text_x = 4; // Offset from widget edge
    int click_char = (x - text_x) / CHAR_WIDTH + tb->scroll_offset;

    if (click_char < 0)
        click_char = 0;
    if (click_char > tb->text_length)
        click_char = tb->text_length;

    tb->cursor_pos = click_char;
    tb->selection_start = tb->selection_end = click_char;
}

/**
 * @brief Adjusts the scroll offset to keep the cursor visible.
 *
 * This internal helper is called after cursor movement to ensure the cursor
 * remains within the visible portion of the textbox. It adjusts scroll_offset
 * as needed:
 *
 * - If cursor moves left of visible area, scroll left to show it
 * - If cursor moves right of visible area, scroll right to show it
 *
 * @param tb Pointer to the textbox widget.
 *
 * @note The visible character count is calculated as (width - 8) / CHAR_WIDTH.
 *
 * @see textbox_key() Which calls this after cursor movement
 * @see textbox_insert_char() Which calls this after character insertion
 */
static void textbox_ensure_cursor_visible(textbox_t *tb) {
    int visible_chars = (tb->base.width - 8) / CHAR_WIDTH;

    if (tb->cursor_pos < tb->scroll_offset) {
        tb->scroll_offset = tb->cursor_pos;
    } else if (tb->cursor_pos > tb->scroll_offset + visible_chars) {
        tb->scroll_offset = tb->cursor_pos - visible_chars;
    }
}

/**
 * @brief Deletes the currently selected text.
 *
 * This internal helper removes all characters between selection_start and
 * selection_end, shifting the remaining text to fill the gap. After deletion:
 * - The cursor is positioned at the start of the former selection
 * - The selection is cleared (start == end)
 *
 * If no text is selected (selection_start == selection_end), this function
 * does nothing.
 *
 * @param tb Pointer to the textbox widget.
 *
 * @note The on_change callback is NOT called by this function. The caller
 *       is responsible for invoking on_change if appropriate.
 *
 * @see textbox_insert_char() Which calls this before inserting text
 * @see textbox_key() Which calls this for Backspace/Delete with selection
 */
static void textbox_delete_selection(textbox_t *tb) {
    if (tb->selection_start == tb->selection_end)
        return;

    int sel_start =
        tb->selection_start < tb->selection_end ? tb->selection_start : tb->selection_end;
    int sel_end = tb->selection_start < tb->selection_end ? tb->selection_end : tb->selection_start;

    // Remove selected text
    int remove_len = sel_end - sel_start;
    memmove(tb->text + sel_start, tb->text + sel_end, tb->text_length - sel_end + 1);
    tb->text_length -= remove_len;

    tb->cursor_pos = sel_start;
    tb->selection_start = tb->selection_end = sel_start;
}

/**
 * @brief Inserts a single character at the current cursor position.
 *
 * This internal helper performs the following steps:
 * 1. Delete any selected text (replacing selection with the new character)
 * 2. Grow the text buffer if needed (doubling capacity)
 * 3. Shift existing text right to make room for the new character
 * 4. Insert the character at cursor_pos
 * 5. Update cursor position and text length
 * 6. Ensure cursor remains visible (adjust scroll if needed)
 * 7. Invoke the on_change callback if registered
 *
 * @param tb Pointer to the textbox widget.
 * @param ch The ASCII character to insert (should be printable, 32-126).
 *
 * @note Memory allocation failure during buffer growth is handled silently;
 *       the character is simply not inserted.
 *
 * @see textbox_key() Which calls this for printable character input
 */
static void textbox_insert_char(textbox_t *tb, char ch) {
    // Delete selection first if any
    textbox_delete_selection(tb);

    // Grow buffer if needed
    if (tb->text_length + 1 >= tb->text_capacity) {
        int new_cap = tb->text_capacity * 2;
        char *new_text = (char *)realloc(tb->text, new_cap);
        if (!new_text)
            return;
        tb->text = new_text;
        tb->text_capacity = new_cap;
    }

    // Insert character
    memmove(tb->text + tb->cursor_pos + 1,
            tb->text + tb->cursor_pos,
            tb->text_length - tb->cursor_pos + 1);
    tb->text[tb->cursor_pos] = ch;
    tb->text_length++;
    tb->cursor_pos++;

    textbox_ensure_cursor_visible(tb);

    if (tb->on_change) {
        tb->on_change(tb->callback_data);
    }
}

/**
 * @brief Handles keyboard input for the textbox widget.
 *
 * This key handler processes navigation keys, editing keys, and printable
 * character input. If the textbox is in readonly mode, all input is ignored.
 *
 * ## Supported Keys
 *
 * | Keycode | Key          | Action                                        |
 * |---------|--------------|-----------------------------------------------|
 * | 0x50    | Left Arrow   | Move cursor left one character                |
 * | 0x4F    | Right Arrow  | Move cursor right one character               |
 * | 0x4A    | Home         | Move cursor to start of text                  |
 * | 0x4D    | End          | Move cursor to end of text                    |
 * | 0x2A    | Backspace    | Delete char before cursor, or delete selection|
 * | 0x4C    | Delete       | Delete char after cursor, or delete selection |
 * | 0x28    | Enter        | Invoke on_enter callback                      |
 * | 32-126  | Printable    | Insert character at cursor position           |
 *
 * All navigation keys clear the current selection and reposition the cursor.
 * Backspace and Delete trigger the on_change callback after deleting text.
 *
 * @param w       Pointer to the base widget structure (cast to textbox_t internally).
 * @param keycode The raw evdev keycode for the key press.
 * @param ch      The ASCII character (if printable), or '\0' for non-printable keys.
 *
 * @note The keycodes used are evdev USB HID codes, not ASCII values.
 *       For example, Left Arrow is 0x50 (80), not related to ASCII 'P'.
 *
 * @note Arrow key navigation does not currently support Shift+Arrow for
 *       selection extension. This could be added in a future enhancement.
 *
 * @see textbox_insert_char() For character insertion
 * @see textbox_delete_selection() For selection deletion
 */
static void textbox_key(widget_t *w, int keycode, char ch) {
    textbox_t *tb = (textbox_t *)w;

    if (tb->readonly)
        return;

    switch (keycode) {
        case 0x50: // Left arrow
            if (tb->cursor_pos > 0) {
                tb->cursor_pos--;
                tb->selection_start = tb->selection_end = tb->cursor_pos;
                textbox_ensure_cursor_visible(tb);
            }
            break;

        case 0x4F: // Right arrow
            if (tb->cursor_pos < tb->text_length) {
                tb->cursor_pos++;
                tb->selection_start = tb->selection_end = tb->cursor_pos;
                textbox_ensure_cursor_visible(tb);
            }
            break;

        case 0x4A: // Home
            tb->cursor_pos = 0;
            tb->selection_start = tb->selection_end = 0;
            textbox_ensure_cursor_visible(tb);
            break;

        case 0x4D: // End
            tb->cursor_pos = tb->text_length;
            tb->selection_start = tb->selection_end = tb->text_length;
            textbox_ensure_cursor_visible(tb);
            break;

        case 0x2A: // Backspace
            if (tb->selection_start != tb->selection_end) {
                textbox_delete_selection(tb);
            } else if (tb->cursor_pos > 0) {
                memmove(tb->text + tb->cursor_pos - 1,
                        tb->text + tb->cursor_pos,
                        tb->text_length - tb->cursor_pos + 1);
                tb->text_length--;
                tb->cursor_pos--;
                textbox_ensure_cursor_visible(tb);
            }
            if (tb->on_change) {
                tb->on_change(tb->callback_data);
            }
            break;

        case 0x4C: // Delete
            if (tb->selection_start != tb->selection_end) {
                textbox_delete_selection(tb);
            } else if (tb->cursor_pos < tb->text_length) {
                memmove(tb->text + tb->cursor_pos,
                        tb->text + tb->cursor_pos + 1,
                        tb->text_length - tb->cursor_pos);
                tb->text_length--;
            }
            if (tb->on_change) {
                tb->on_change(tb->callback_data);
            }
            break;

        case 0x28: // Enter
            if (tb->on_enter) {
                tb->on_enter(tb->callback_data);
            }
            break;

        default:
            // Printable character
            if (ch >= 32 && ch < 127) {
                textbox_insert_char(tb, ch);
            }
            break;
    }
}

//===----------------------------------------------------------------------===//
// TextBox API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new single-line text input widget.
 *
 * This function allocates and initializes a textbox widget with an empty
 * text buffer. The textbox is ready for text input once it receives focus.
 *
 * The created textbox has the following default properties:
 * - **Size**: 150x20 pixels (suitable for single-line input)
 * - **Position**: (0, 0) - caller should use widget_set_position() to place it
 * - **Colors**: White background, black text
 * - **State**: Visible, enabled, not readonly, not password mode
 * - **Buffer**: 256 bytes initial capacity, grows as needed
 * - **Cursor**: At position 0, no selection
 *
 * ## Memory Management
 *
 * The textbox allocates its text buffer dynamically. When destroyed via
 * widget_destroy(), the text buffer is also freed. If creation fails
 * partway through (e.g., buffer allocation fails), all resources are
 * cleaned up before returning NULL.
 *
 * ## Example Usage
 *
 * @code
 * // Create a login form
 * textbox_t *username = textbox_create(form);
 * widget_set_position((widget_t *)username, 80, 30);
 * widget_set_size((widget_t *)username, 200, 20);
 *
 * textbox_t *password = textbox_create(form);
 * widget_set_position((widget_t *)password, 80, 60);
 * widget_set_size((widget_t *)password, 200, 20);
 * textbox_set_password_mode(password, true);
 *
 * textbox_set_onenter(password, on_login_submit, form);
 * @endcode
 *
 * @param parent Pointer to the parent widget container. If non-NULL, the
 *               textbox is added to this parent's child list. Pass NULL
 *               for textboxes managed independently.
 *
 * @return Pointer to the newly created textbox, or NULL if memory allocation
 *         failed. The returned pointer can be cast to widget_t* for use
 *         with generic widget functions.
 *
 * @see textbox_set_text() To set initial text content
 * @see textbox_set_password_mode() For password entry fields
 * @see textbox_set_readonly() For display-only text fields
 */
textbox_t *textbox_create(widget_t *parent) {
    textbox_t *tb = (textbox_t *)malloc(sizeof(textbox_t));
    if (!tb)
        return NULL;

    memset(tb, 0, sizeof(textbox_t));

    // Initialize base widget
    tb->base.type = WIDGET_TEXTBOX;
    tb->base.parent = parent;
    tb->base.visible = true;
    tb->base.enabled = true;
    tb->base.bg_color = WB_WHITE;
    tb->base.fg_color = WB_BLACK;
    tb->base.width = 150;
    tb->base.height = 20;

    // Set handlers
    tb->base.on_paint = textbox_paint;
    tb->base.on_click = textbox_click;
    tb->base.on_key = textbox_key;

    // Allocate text buffer
    tb->text_capacity = TEXTBOX_INITIAL_CAPACITY;
    tb->text = (char *)malloc(tb->text_capacity);
    if (!tb->text) {
        free(tb);
        return NULL;
    }
    tb->text[0] = '\0';
    tb->text_length = 0;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)tb);
    }

    return tb;
}

/**
 * @brief Sets the text content of a textbox, replacing any existing text.
 *
 * This function replaces the entire textbox content with the provided string.
 * The text buffer is grown if necessary to accommodate the new text. After
 * setting the text:
 * - Cursor is reset to position 0
 * - Scroll offset is reset to 0 (showing the start of text)
 * - Any selection is cleared
 *
 * @param tb   Pointer to the textbox widget. If NULL, does nothing.
 * @param text The new text content. If NULL, the textbox is cleared to empty.
 *             The text is copied; the caller retains ownership of the original.
 *
 * @note The on_change callback is NOT invoked by this function, as programmatic
 *       changes are typically distinct from user edits. If you need to trigger
 *       change handling, call the callback manually.
 *
 * @note This function does not trigger a repaint. Call widget_app_repaint()
 *       to see the new text.
 *
 * @see textbox_get_text() To retrieve the current text
 */
void textbox_set_text(textbox_t *tb, const char *text) {
    if (!tb)
        return;

    int len = text ? (int)strlen(text) : 0;

    // Grow buffer if needed
    if (len + 1 > tb->text_capacity) {
        int new_cap = len + 1;
        char *new_text = (char *)realloc(tb->text, new_cap);
        if (!new_text)
            return;
        tb->text = new_text;
        tb->text_capacity = new_cap;
    }

    if (text) {
        strcpy(tb->text, text);
        tb->text_length = len;
    } else {
        tb->text[0] = '\0';
        tb->text_length = 0;
    }

    tb->cursor_pos = 0;
    tb->scroll_offset = 0;
    tb->selection_start = tb->selection_end = 0;
}

/**
 * @brief Retrieves the current text content of a textbox.
 *
 * This function returns a pointer to the textbox's internal text buffer.
 * The returned string is owned by the textbox and should not be modified
 * or freed by the caller.
 *
 * @param tb Pointer to the textbox widget. If NULL, returns NULL.
 *
 * @return Pointer to the textbox's text string (null-terminated), or NULL
 *         if tb is NULL. The returned pointer remains valid until the
 *         textbox is destroyed or text is modified.
 *
 * @note For password fields, this returns the actual password text, not
 *       asterisks. The asterisks are only a display feature.
 *
 * @see textbox_set_text() To change the text content
 */
const char *textbox_get_text(textbox_t *tb) {
    return tb ? tb->text : NULL;
}

/**
 * @brief Enables or disables password mode for a textbox.
 *
 * In password mode, the textbox displays asterisks (*) instead of the actual
 * characters being typed. This provides visual privacy for password entry
 * while still allowing the actual password to be retrieved via textbox_get_text().
 *
 * Password mode only affects the visual display. All editing operations
 * (cursor movement, selection, deletion) work the same as normal mode.
 *
 * @param tb      Pointer to the textbox widget. If NULL, does nothing.
 * @param enabled True to enable password mode (show asterisks), false to
 *                show actual text.
 *
 * @note Toggling password mode does not clear the text content. You can
 *       implement a "show password" toggle by calling this function.
 *
 * @note Trigger a repaint after changing this setting to see the effect.
 *
 * @see textbox_paint() Where password mode affects rendering
 */
void textbox_set_password_mode(textbox_t *tb, bool enabled) {
    if (tb) {
        tb->password_mode = enabled;
    }
}

/**
 * @brief Enables or disables multiline mode for a textbox.
 *
 * @note **NOT CURRENTLY IMPLEMENTED**. The textbox is always single-line.
 *       This function sets the internal flag but the rendering and input
 *       handling do not yet support multiline editing.
 *
 * When fully implemented, multiline mode would:
 * - Allow Enter to insert newlines instead of triggering on_enter
 * - Render multiple lines of text
 * - Support vertical scrolling
 * - Handle Up/Down arrow keys for line navigation
 *
 * @param tb      Pointer to the textbox widget. If NULL, does nothing.
 * @param enabled True to enable multiline mode, false for single-line.
 *
 * @todo Implement multiline text editing and rendering.
 */
void textbox_set_multiline(textbox_t *tb, bool enabled) {
    if (tb) {
        tb->multiline = enabled;
    }
}

/**
 * @brief Enables or disables read-only mode for a textbox.
 *
 * In read-only mode, the textbox displays text but does not accept any
 * keyboard input. The user cannot type, delete, or otherwise modify the
 * content. However:
 * - The textbox can still receive focus (for accessibility)
 * - The cursor is not displayed
 * - The text can still be programmatically changed via textbox_set_text()
 *
 * Read-only textboxes are useful for displaying values that the user should
 * see but not modify, such as calculated results or system information.
 *
 * @param tb       Pointer to the textbox widget. If NULL, does nothing.
 * @param readonly True to make the textbox read-only, false to allow editing.
 *
 * @note Read-only mode does not change the visual appearance beyond hiding
 *       the cursor. Consider using a label if you don't need the sunken
 *       frame appearance.
 *
 * @see textbox_key() Where readonly mode blocks input
 */
void textbox_set_readonly(textbox_t *tb, bool readonly) {
    if (tb) {
        tb->readonly = readonly;
    }
}

/**
 * @brief Registers a callback for text change events.
 *
 * The on_change callback is invoked whenever the text content is modified
 * by user input. This includes:
 * - Typing a character
 * - Deleting a character with Backspace or Delete
 * - Deleting selected text
 *
 * The callback is NOT invoked for programmatic changes via textbox_set_text().
 *
 * @param tb       Pointer to the textbox widget. If NULL, does nothing.
 * @param callback The function to call when text changes, or NULL to remove
 *                 any existing callback.
 * @param data     User-defined data passed to the callback function.
 *
 * @note The callback is invoked synchronously from within the key handler,
 *       before the repaint. Long-running callbacks will delay the visual
 *       update and block the event loop.
 *
 * @warning Both on_change and on_enter share the same callback_data pointer.
 *          Setting on_change will overwrite any data set for on_enter.
 *
 * @see textbox_set_onenter() For Enter key handling
 */
void textbox_set_onchange(textbox_t *tb, widget_callback_fn callback, void *data) {
    if (tb) {
        tb->on_change = callback;
        tb->callback_data = data;
    }
}

/**
 * @brief Registers a callback for Enter key press events.
 *
 * The on_enter callback is invoked when the user presses Enter while the
 * textbox has focus. This is commonly used to:
 * - Submit a form when pressing Enter in a text field
 * - Trigger a search when pressing Enter in a search box
 * - Move focus to the next field in a sequence
 *
 * @param tb       Pointer to the textbox widget. If NULL, does nothing.
 * @param callback The function to call when Enter is pressed, or NULL to
 *                 remove any existing callback.
 * @param data     User-defined data passed to the callback function.
 *
 * @note In single-line mode, Enter triggers the callback without inserting
 *       a newline. In multiline mode (when implemented), Enter would insert
 *       a newline and a different key (e.g., Ctrl+Enter) might be needed
 *       for form submission.
 *
 * @warning Both on_change and on_enter share the same callback_data pointer.
 *          Setting on_enter will overwrite any data set for on_change.
 *
 * @see textbox_set_onchange() For text change handling
 */
void textbox_set_onenter(textbox_t *tb, widget_callback_fn callback, void *data) {
    if (tb) {
        tb->on_enter = callback;
        tb->callback_data = data;
    }
}

/**
 * @brief Retrieves the current cursor position within the text.
 *
 * The cursor position is a zero-based character index indicating where
 * the next typed character would be inserted. A value of 0 means the
 * cursor is at the beginning of the text; a value equal to text_length
 * means the cursor is at the end.
 *
 * @param tb Pointer to the textbox widget. If NULL, returns 0.
 *
 * @return The cursor position (0 to text_length), or 0 if tb is NULL.
 *
 * @see textbox_set_cursor_pos() To change the cursor position
 */
int textbox_get_cursor_pos(textbox_t *tb) {
    return tb ? tb->cursor_pos : 0;
}

/**
 * @brief Sets the cursor position within the text.
 *
 * This function moves the cursor to the specified character position and
 * clears any text selection. The scroll offset is adjusted if necessary
 * to keep the cursor visible.
 *
 * @param tb  Pointer to the textbox widget. If NULL, does nothing.
 * @param pos The new cursor position. Values less than 0 are clamped to 0.
 *            Values greater than text_length are clamped to text_length.
 *
 * @note This function clears any existing selection. If you need to set
 *       the cursor while preserving selection, manipulate the fields directly.
 *
 * @see textbox_get_cursor_pos() To retrieve the current position
 * @see textbox_select_all() To select all text
 */
void textbox_set_cursor_pos(textbox_t *tb, int pos) {
    if (!tb)
        return;

    if (pos < 0)
        pos = 0;
    if (pos > tb->text_length)
        pos = tb->text_length;

    tb->cursor_pos = pos;
    tb->selection_start = tb->selection_end = pos;
    textbox_ensure_cursor_visible(tb);
}

/**
 * @brief Selects all text in the textbox.
 *
 * This function creates a selection spanning the entire text content:
 * - selection_start is set to 0 (beginning of text)
 * - selection_end is set to text_length (end of text)
 * - cursor is positioned at the end of the selection
 *
 * This is useful for implementing "select all" functionality (e.g., Ctrl+A)
 * or for pre-selecting text in a field so the user can easily replace it.
 *
 * @param tb Pointer to the textbox widget. If NULL, does nothing.
 *
 * @note Trigger a repaint after calling this to see the selection highlight.
 *
 * @see textbox_clear_selection() To remove the selection
 */
void textbox_select_all(textbox_t *tb) {
    if (tb) {
        tb->selection_start = 0;
        tb->selection_end = tb->text_length;
        tb->cursor_pos = tb->text_length;
    }
}

/**
 * @brief Clears any text selection in the textbox.
 *
 * This function removes the selection by setting selection_start and
 * selection_end to match the current cursor position. The cursor position
 * itself is not changed.
 *
 * @param tb Pointer to the textbox widget. If NULL, does nothing.
 *
 * @note Trigger a repaint after calling this to remove the selection highlight.
 *
 * @see textbox_select_all() To select all text
 */
void textbox_clear_selection(textbox_t *tb) {
    if (tb) {
        tb->selection_start = tb->selection_end = tb->cursor_pos;
    }
}
