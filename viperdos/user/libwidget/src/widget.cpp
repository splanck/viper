//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file widget.c
 * @brief Core widget system implementation for the libwidget toolkit.
 *
 * This file contains the fundamental widget operations that form the backbone
 * of the libwidget GUI toolkit. It implements:
 *
 * - **Widget Lifecycle**: Creation, initialization, and destruction of widgets
 * - **Geometry Management**: Position and size manipulation functions
 * - **Widget Hierarchy**: Parent-child relationships and tree traversal
 * - **Event Handling**: Mouse click and keyboard event dispatch
 * - **Focus Management**: Keyboard focus tracking across widget trees
 * - **Rendering Pipeline**: Paint dispatch and child rendering
 * - **Application Framework**: Complete event loop with GUI integration
 *
 * ## Architecture Overview
 *
 * The widget system uses a tree-based hierarchy where each widget can have
 * multiple children. Events are dispatched using a bubbling model where
 * child widgets are given first opportunity to handle events (in reverse
 * z-order for correct visual layering).
 *
 * The rendering pipeline works top-down: parents paint first, then children
 * are painted on top. This allows container widgets to draw backgrounds
 * that children will appear over.
 *
 * ## Memory Management
 *
 * Widgets own their children. When a parent is destroyed, all descendants
 * are recursively destroyed. The widget_destroy() function handles this
 * automatically, including removing the widget from its parent's child list.
 *
 * ## Coordinate System
 *
 * All widget coordinates are in absolute window coordinates. When handling
 * events like mouse clicks, the coordinates are transformed to widget-local
 * coordinates before being passed to callbacks (by subtracting widget origin).
 *
 * @see widget.h for public API declarations
 * @see layout.c for automatic widget positioning
 * @see Individual widget type files (button.c, textbox.c, etc.) for specific implementations
 */
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Keycode to character conversion
//===----------------------------------------------------------------------===//

/**
 * @brief Converts an evdev keycode to its corresponding ASCII character.
 *
 * This function translates Linux evdev keyboard scancodes into printable
 * ASCII characters. It handles the standard US QWERTY keyboard layout
 * including letters, numbers, and common punctuation marks.
 *
 * The evdev keycode values used are:
 * - **Q-P row**: keycodes 16-25 → "qwertyuiop"
 * - **A-L row**: keycodes 30-38 → "asdfghjkl"
 * - **Z-M row**: keycodes 44-50 → "zxcvbnm"
 * - **Number row**: keycodes 2-10 → '1'-'9', keycode 11 → '0'
 * - **Special keys**: keycode 57 → space, 12 → '-'/'_', 13 → '='/'+'
 * - **Punctuation**: keycode 52 → '.'/'>', keycode 51 → ','/'<'
 *
 * The function respects the shift modifier for:
 * - Uppercase letters (a-z → A-Z)
 * - Number row symbols (1-0 → !@#$%^&*())
 * - Punctuation alternatives (- → _, = → +, . → >, , → <)
 *
 * @param keycode  The evdev keycode to convert. Valid keycodes are those
 *                 listed above; other keycodes result in a null character.
 * @param modifiers Modifier key bitmask. Bit 0 (value 1) indicates shift
 *                  is held, which affects the output character.
 *
 * @return The ASCII character corresponding to the keycode, or '\0' (null)
 *         if the keycode doesn't map to a printable character.
 *
 * @note This function only handles a subset of the keyboard. Control keys,
 *       function keys, navigation keys, and other special keys are not
 *       translated and will return '\0'.
 *
 * @note The evdev keycodes used here are Linux-standard values that differ
 *       from USB HID scancodes and Windows virtual key codes.
 *
 * @see widget_handle_key() Uses this to provide character data to key handlers
 */
static char keycode_to_char(uint16_t keycode, uint8_t modifiers) {
    bool shift = (modifiers & 1) != 0;
    char ch = 0;

    // Letters (evdev: Q=16..P=25, A=30..L=38, Z=44..M=50)
    if (keycode >= 16 && keycode <= 25) {
        ch = "qwertyuiop"[keycode - 16];
    } else if (keycode >= 30 && keycode <= 38) {
        ch = "asdfghjkl"[keycode - 30];
    } else if (keycode >= 44 && keycode <= 50) {
        ch = "zxcvbnm"[keycode - 44];
    } else if (keycode >= 2 && keycode <= 10) {
        ch = shift ? "!@#$%^&*("[keycode - 2] : '1' + (keycode - 2);
    } else if (keycode == 11) {
        ch = shift ? ')' : '0';
    } else if (keycode == 57) {
        ch = ' ';
    } else if (keycode == 12) {
        ch = shift ? '_' : '-';
    } else if (keycode == 13) {
        ch = shift ? '+' : '=';
    } else if (keycode == 52) {
        ch = shift ? '>' : '.';
    } else if (keycode == 51) {
        ch = shift ? '<' : ',';
    }

    if (ch && shift && ch >= 'a' && ch <= 'z') {
        ch = ch - 'a' + 'A';
    }

    return ch;
}

//===----------------------------------------------------------------------===//
// Internal Helpers
//===----------------------------------------------------------------------===//

/**
 * @brief Initializes the base fields of a widget structure.
 *
 * This internal helper function sets up the common fields present in all
 * widget types. It is called by both widget_create() for generic widgets
 * and by type-specific creation functions (button_create, textbox_create, etc.)
 * to ensure consistent initialization across all widget types.
 *
 * The initialization process:
 * 1. Zeros the entire widget structure using memset
 * 2. Sets the widget type identifier
 * 3. Establishes parent relationship
 * 4. Sets default visibility to true (widgets are visible by default)
 * 5. Sets default enabled state to true (widgets are interactive by default)
 * 6. Applies default colors (light gray background, black foreground)
 * 7. Adds this widget to the parent's child list (if parent is not NULL)
 *
 * @param w      Pointer to the widget structure to initialize. Must point to
 *               already-allocated memory of at least sizeof(widget_t) bytes.
 * @param type   The type of widget being created (WIDGET_BUTTON, WIDGET_LABEL, etc.).
 *               This determines how the widget is rendered and how it handles events.
 * @param parent Optional parent widget. If non-NULL, this widget is automatically
 *               added to the parent's child list. Pass NULL for root widgets.
 *
 * @note This function assumes `w` points to valid, allocated memory.
 *       The caller is responsible for memory allocation.
 *
 * @note The memset to zero ensures all pointers (children, callbacks, user_data)
 *       start as NULL and all counts start at zero.
 *
 * @see widget_create() Public function for creating generic widgets
 * @see button_create(), textbox_create(), etc. for type-specific creation
 */
static void widget_init_base(widget_t *w, widget_type_t type, widget_t *parent) {
    memset(w, 0, sizeof(widget_t));
    w->type = type;
    w->parent = parent;
    w->visible = true;
    w->enabled = true;
    w->bg_color = WB_GRAY_LIGHT;
    w->fg_color = WB_BLACK;

    if (parent) {
        widget_add_child(parent, w);
    }
}

/**
 * @brief Recursively frees all child widgets and releases the children array.
 *
 * This internal helper function is called during widget destruction to clean
 * up the entire subtree of child widgets. It performs a depth-first traversal,
 * destroying each child (which in turn destroys their children) before freeing
 * the children array itself.
 *
 * The destruction process:
 * 1. If the children array exists:
 *    a. Iterate through each child pointer
 *    b. Call widget_destroy() on each child (recursive destruction)
 *    c. Free the children pointer array
 *    d. Reset children pointer to NULL
 *    e. Reset child_count and child_capacity to 0
 *
 * @param w Pointer to the widget whose children should be destroyed.
 *          The widget itself is NOT destroyed—only its children.
 *
 * @warning This function should only be called during widget destruction.
 *          Calling it on an active widget will orphan the parent (leaving it
 *          with no children) and may cause rendering issues.
 *
 * @note The children are destroyed in forward order (index 0 first), which
 *       shouldn't matter for destruction but differs from event handling
 *       (which uses reverse order for z-ordering).
 *
 * @see widget_destroy() The public destruction function that calls this helper
 */
static void widget_free_children(widget_t *w) {
    if (w->children) {
        for (int i = 0; i < w->child_count; i++) {
            widget_destroy(w->children[i]);
        }
        free(w->children);
        w->children = NULL;
        w->child_count = 0;
        w->child_capacity = 0;
    }
}

//===----------------------------------------------------------------------===//
// Core Widget Functions
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new generic widget of the specified type.
 *
 * This function allocates and initializes a basic widget structure. It is
 * primarily used for container widgets or custom widget types that don't
 * require additional type-specific data. For standard widget types like
 * buttons, labels, or textboxes, use the type-specific creation functions
 * (button_create(), label_create(), etc.) which allocate the correct
 * structure size and perform additional initialization.
 *
 * The created widget has the following default properties:
 * - **Visible**: true (widget will be rendered)
 * - **Enabled**: true (widget will respond to events)
 * - **Geometry**: (0, 0, 0, 0) - position and size are zero
 * - **Colors**: Light gray background, black foreground
 * - **Focus**: Not focused
 * - **Callbacks**: All NULL (no event handlers)
 * - **Children**: Empty (no child widgets)
 *
 * @param type   The type of widget to create. For generic containers, use
 *               WIDGET_CONTAINER. For custom widgets, any type value can
 *               be used if you plan to handle rendering manually.
 * @param parent Optional parent widget. If non-NULL, the new widget is
 *               automatically added to the parent's child list. Pass NULL
 *               for top-level widgets (like root containers).
 *
 * @return Pointer to the newly created widget, or NULL if memory allocation
 *         failed. The caller is responsible for calling widget_destroy()
 *         when the widget is no longer needed (unless a parent will destroy it).
 *
 * @note Memory is allocated using malloc(). The returned pointer must be
 *       freed via widget_destroy(), not direct free() calls, to ensure
 *       proper cleanup of children and layout managers.
 *
 * @see widget_destroy() To free the widget and its descendants
 * @see button_create(), label_create(), etc. for type-specific widgets
 */
widget_t *widget_create(widget_type_t type, widget_t *parent) {
    widget_t *w = (widget_t *)malloc(sizeof(widget_t));
    if (!w)
        return NULL;

    widget_init_base(w, type, parent);
    return w;
}

/**
 * @brief Destroys a widget and all its descendants, freeing all resources.
 *
 * This function performs complete cleanup of a widget tree starting from
 * the specified widget. It handles all the necessary bookkeeping to maintain
 * tree integrity and prevent memory leaks or dangling pointers.
 *
 * The destruction process performs these steps in order:
 * 1. **Parent Removal**: If the widget has a parent, it is removed from
 *    the parent's child list. This prevents the parent from referencing
 *    freed memory after destruction.
 * 2. **Child Destruction**: All child widgets are recursively destroyed.
 *    This ensures the entire subtree is cleaned up.
 * 3. **Layout Cleanup**: If the widget has an attached layout manager,
 *    it is destroyed via layout_destroy().
 * 4. **Memory Release**: The widget structure itself is freed.
 *
 * @param w Pointer to the widget to destroy. If NULL, this function does
 *          nothing (safe to call with NULL).
 *
 * @warning After calling this function, the pointer `w` and all pointers
 *          to its descendant widgets become invalid. Do not use them.
 *
 * @warning Do not call widget_destroy() on a widget that is currently
 *          being iterated over (e.g., during event handling). This can
 *          cause undefined behavior. Instead, mark widgets for deletion
 *          and destroy them after the event loop iteration completes.
 *
 * @note Type-specific widgets (buttons, textboxes, etc.) may have additional
 *       resources. Their creation functions allocate extended structures,
 *       but those structures embed widget_t as the first member, so this
 *       generic destroy function works correctly for all widget types.
 *
 * @see widget_create() To create a widget
 * @see widget_remove_child() Called internally to update parent
 */
void widget_destroy(widget_t *w) {
    if (!w)
        return;

    // Remove from parent
    if (w->parent) {
        widget_remove_child(w->parent, w);
    }

    // Free children
    widget_free_children(w);

    // Free layout
    if (w->layout) {
        layout_destroy(w->layout);
    }

    free(w);
}

/**
 * @brief Sets the position of a widget within its parent's coordinate space.
 *
 * This function updates the widget's x and y coordinates, which determine
 * where the widget is rendered within the window. Coordinates are in
 * absolute window space—they are not relative to the parent widget.
 *
 * Position changes take effect on the next repaint. If the widget is
 * managed by a layout manager, the layout may override this position
 * during its next layout pass.
 *
 * @param w Pointer to the widget to reposition. If NULL, this function
 *          does nothing (safe to call with NULL).
 * @param x The new x-coordinate (horizontal position from window left edge).
 *          A value of 0 places the widget at the left edge of the window.
 * @param y The new y-coordinate (vertical position from window top edge).
 *          A value of 0 places the widget at the top edge of the window.
 *
 * @note This function does not trigger a repaint. Call widget_app_repaint()
 *       or widget_repaint() to see the visual change.
 *
 * @see widget_set_size() To change widget dimensions
 * @see widget_set_geometry() To set position and size together
 * @see widget_get_geometry() To retrieve current position and size
 */
void widget_set_position(widget_t *w, int x, int y) {
    if (w) {
        w->x = x;
        w->y = y;
    }
}

/**
 * @brief Sets the dimensions of a widget.
 *
 * This function updates the widget's width and height, which determine
 * the bounds used for rendering and hit testing. All drawing operations
 * should stay within these bounds, and mouse events are only delivered
 * if the click occurs within the widget's geometry.
 *
 * Size changes take effect on the next repaint. If the widget is managed
 * by a layout manager, the layout may override this size during its next
 * layout pass (depending on the layout policy).
 *
 * @param w      Pointer to the widget to resize. If NULL, this function
 *               does nothing (safe to call with NULL).
 * @param width  The new width in pixels. Must be >= 0. A width of 0 makes
 *               the widget invisible for hit testing purposes.
 * @param height The new height in pixels. Must be >= 0. A height of 0 makes
 *               the widget invisible for hit testing purposes.
 *
 * @note Child widgets are NOT automatically repositioned or resized when
 *       a parent's size changes. Use layout managers to handle automatic
 *       child arrangement, or manually update children after resize.
 *
 * @note This function does not trigger a repaint. Call widget_app_repaint()
 *       to see the visual change.
 *
 * @see widget_set_position() To change widget position
 * @see widget_set_geometry() To set position and size together
 * @see layout_apply() To recalculate child positions after resize
 */
void widget_set_size(widget_t *w, int width, int height) {
    if (w) {
        w->width = width;
        w->height = height;
    }
}

/**
 * @brief Sets the complete geometry (position and size) of a widget.
 *
 * This is a convenience function that sets both the position and dimensions
 * of a widget in a single call. It is equivalent to calling widget_set_position()
 * followed by widget_set_size(), but may be more convenient when you have
 * all four values available.
 *
 * @param w      Pointer to the widget to configure. If NULL, this function
 *               does nothing (safe to call with NULL).
 * @param x      The new x-coordinate (horizontal position from window left edge).
 * @param y      The new y-coordinate (vertical position from window top edge).
 * @param width  The new width in pixels.
 * @param height The new height in pixels.
 *
 * @note This function does not trigger a repaint. Call widget_app_repaint()
 *       to see the visual change.
 *
 * @see widget_set_position() To change only position
 * @see widget_set_size() To change only size
 * @see widget_get_geometry() To retrieve current geometry
 */
void widget_set_geometry(widget_t *w, int x, int y, int width, int height) {
    if (w) {
        w->x = x;
        w->y = y;
        w->width = width;
        w->height = height;
    }
}

/**
 * @brief Retrieves the current geometry (position and size) of a widget.
 *
 * This function returns the widget's current position and dimensions through
 * output parameters. Any output parameter may be NULL if that value is not
 * needed, allowing selective retrieval.
 *
 * @param w      Pointer to the widget to query. If NULL, this function does
 *               nothing and does not modify any output parameters.
 * @param[out] x Pointer to receive the x-coordinate, or NULL if not needed.
 * @param[out] y Pointer to receive the y-coordinate, or NULL if not needed.
 * @param[out] width  Pointer to receive the width, or NULL if not needed.
 * @param[out] height Pointer to receive the height, or NULL if not needed.
 *
 * @note The returned coordinates are in absolute window space, not relative
 *       to the parent widget.
 *
 * @code
 * int x, y, w, h;
 * widget_get_geometry(my_widget, &x, &y, &w, &h);
 * printf("Widget at (%d, %d), size %dx%d\n", x, y, w, h);
 *
 * // Or get just the position:
 * widget_get_geometry(my_widget, &x, &y, NULL, NULL);
 * @endcode
 *
 * @see widget_set_geometry() To modify the geometry
 */
void widget_get_geometry(widget_t *w, int *x, int *y, int *width, int *height) {
    if (w) {
        if (x)
            *x = w->x;
        if (y)
            *y = w->y;
        if (width)
            *width = w->width;
        if (height)
            *height = w->height;
    }
}

/**
 * @brief Controls whether a widget is rendered and receives events.
 *
 * Invisible widgets are completely skipped during rendering—they do not
 * paint themselves or their children. Additionally, invisible widgets
 * do not receive any events (mouse clicks, key presses).
 *
 * Setting a widget invisible effectively hides the entire subtree rooted
 * at that widget. Child widgets inherit the parent's visibility for
 * rendering purposes (if the parent is invisible, children are not rendered
 * even if their own visible flag is true).
 *
 * @param w       Pointer to the widget. If NULL, this function does nothing.
 * @param visible True to make the widget visible, false to hide it.
 *
 * @note Unlike some toolkits, there is no separate "shown" vs "visible"
 *       distinction. Setting visible=true immediately makes the widget
 *       renderable (though repaint must still be triggered).
 *
 * @note This function does not trigger a repaint. To see the change, call
 *       widget_app_repaint().
 *
 * @see widget_is_visible() To query the current visibility state
 * @see widget_set_enabled() For interactive (but still visible) control
 */
void widget_set_visible(widget_t *w, bool visible) {
    if (w) {
        w->visible = visible;
    }
}

/**
 * @brief Controls whether a widget responds to user interaction.
 *
 * Disabled widgets are still rendered but do not respond to mouse clicks
 * or keyboard input. They are typically rendered in a "grayed out" style
 * to indicate their disabled state (though this depends on each widget
 * type's paint implementation).
 *
 * Unlike visibility, disabling a widget does NOT affect its children.
 * Each widget maintains its own enabled state independently.
 *
 * @param w       Pointer to the widget. If NULL, this function does nothing.
 * @param enabled True to enable the widget for interaction, false to disable.
 *
 * @note Disabled widgets still participate in the render tree—they are
 *       painted, just typically with a disabled appearance.
 *
 * @note This function does not trigger a repaint. To see the visual change
 *       (grayed out appearance), call widget_app_repaint().
 *
 * @see widget_is_enabled() To query the current enabled state
 * @see widget_set_visible() To hide widgets completely
 */
void widget_set_enabled(widget_t *w, bool enabled) {
    if (w) {
        w->enabled = enabled;
    }
}

/**
 * @brief Queries whether a widget is currently visible.
 *
 * This function returns the widget's own visibility flag. Note that a widget
 * may have visible=true but still not be rendered if an ancestor widget is
 * invisible (visibility is inherited during rendering).
 *
 * @param w Pointer to the widget to query. If NULL, returns false.
 *
 * @return True if the widget's visibility flag is set, false if the widget
 *         is marked invisible or if w is NULL.
 *
 * @note This returns the widget's local visibility state, not whether the
 *       widget will actually appear on screen (which depends on ancestors).
 *
 * @see widget_set_visible() To change visibility
 */
bool widget_is_visible(widget_t *w) {
    return w ? w->visible : false;
}

/**
 * @brief Queries whether a widget is currently enabled for interaction.
 *
 * Enabled widgets respond to mouse clicks and keyboard input. Disabled
 * widgets are rendered but do not process input events.
 *
 * @param w Pointer to the widget to query. If NULL, returns false.
 *
 * @return True if the widget is enabled, false if disabled or if w is NULL.
 *
 * @see widget_set_enabled() To change the enabled state
 */
bool widget_is_enabled(widget_t *w) {
    return w ? w->enabled : false;
}

/**
 * @brief Sets the foreground and background colors for a widget.
 *
 * These colors are used by the widget's paint handler to render its content.
 * The exact usage depends on the widget type:
 * - **Buttons**: bg_color for button face, fg_color for text
 * - **Labels**: fg_color for text, bg_color typically unused (transparent)
 * - **Textboxes**: bg_color for text area background, fg_color for text
 * - **Containers**: bg_color may be used to fill the background area
 *
 * Color values are 32-bit ARGB format (0xAARRGGBB), where AA is alpha,
 * RR is red, GG is green, and BB is blue. For opaque colors, use 0xFF
 * for the alpha channel (e.g., 0xFF000000 for black).
 *
 * @param w  Pointer to the widget. If NULL, this function does nothing.
 * @param fg The foreground color in ARGB format (typically used for text).
 * @param bg The background color in ARGB format (typically used for fills).
 *
 * @note This function does not trigger a repaint. Call widget_app_repaint()
 *       to see the color change.
 *
 * @note Standard Amiga Workbench colors are available as constants in colors.hpp
 *       (WB_BLACK, WB_WHITE, WB_GRAY_LIGHT, WB_BLUE, etc.).
 *
 * @see colors.hpp for standard color constants
 */
void widget_set_colors(widget_t *w, uint32_t fg, uint32_t bg) {
    if (w) {
        w->fg_color = fg;
        w->bg_color = bg;
    }
}

/**
 * @brief Gives keyboard focus to a widget.
 *
 * The focused widget receives keyboard events (key presses). Only one widget
 * can have focus at a time within a window. When focus is set on a widget:
 *
 * 1. All sibling widgets (children of the same parent) lose focus
 * 2. The on_focus callback is called with focused=false for the previously
 *    focused sibling (if any)
 * 3. The target widget's focused flag is set to true
 * 4. The on_focus callback is called with focused=true for the target widget
 *
 * The focus model is sibling-based: only siblings compete for focus. Widgets
 * in different branches of the tree can theoretically both be "focused" in
 * their respective sibling groups, but keyboard events are dispatched by
 * searching for a focused widget starting from the root.
 *
 * @param w Pointer to the widget that should receive focus. If NULL, this
 *          function does nothing.
 *
 * @note Focus is typically set automatically when a focusable widget (like
 *       a textbox) is clicked. You can also set focus programmatically
 *       using this function.
 *
 * @note This function does not trigger a repaint. If the widget changes its
 *       appearance when focused (e.g., showing a focus border), call
 *       widget_app_repaint() after setting focus.
 *
 * @see widget_has_focus() To check if a widget is focused
 * @see on_focus callback for focus change notifications
 */
void widget_set_focus(widget_t *w) {
    if (w) {
        // Clear focus from siblings
        if (w->parent) {
            for (int i = 0; i < w->parent->child_count; i++) {
                widget_t *sibling = w->parent->children[i];
                if (sibling->focused && sibling != w) {
                    sibling->focused = false;
                    if (sibling->on_focus) {
                        sibling->on_focus(sibling, false);
                    }
                }
            }
        }
        w->focused = true;
        if (w->on_focus) {
            w->on_focus(w, true);
        }
    }
}

/**
 * @brief Checks whether a widget currently has keyboard focus.
 *
 * A focused widget is one that will receive keyboard events. The focused
 * state is typically indicated visually (e.g., a focus border around
 * text fields).
 *
 * @param w Pointer to the widget to query. If NULL, returns false.
 *
 * @return True if the widget is focused, false otherwise.
 *
 * @see widget_set_focus() To give focus to a widget
 */
bool widget_has_focus(widget_t *w) {
    return w ? w->focused : false;
}

/**
 * @brief Adds a widget as a child of another widget.
 *
 * This function establishes a parent-child relationship between two widgets.
 * The child is added to the end of the parent's children array, meaning it
 * will render on top of earlier children (higher z-order) and receive event
 * handling priority.
 *
 * The children array grows dynamically as needed. Initial allocation is
 * 4 slots, doubling each time capacity is exceeded (4 → 8 → 16 → 32 ...).
 *
 * @param parent Pointer to the widget that will become the parent.
 *               If NULL, this function does nothing.
 * @param child  Pointer to the widget to add as a child.
 *               If NULL, this function does nothing.
 *
 * @note If the child already has a parent, it will now have TWO parents,
 *       which is undefined behavior. Use widget_remove_child() first if
 *       you need to reparent a widget.
 *
 * @note This function updates the child's parent pointer to reference
 *       the parent widget.
 *
 * @note Memory allocation failure (realloc returns NULL) is silently
 *       ignored—the child is not added but no error is reported.
 *
 * @see widget_remove_child() To remove a child from a parent
 * @see widget_create() Automatically adds to parent if parent is non-NULL
 */
void widget_add_child(widget_t *parent, widget_t *child) {
    if (!parent || !child)
        return;

    // Grow array if needed
    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity ? parent->child_capacity * 2 : 4;
        widget_t **new_children = (widget_t **)realloc(parent->children, new_cap * sizeof(widget_t *));
        if (!new_children)
            return;
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

/**
 * @brief Removes a widget from its parent's children list.
 *
 * This function breaks the parent-child relationship without destroying
 * either widget. After removal:
 * - The child is no longer in the parent's children array
 * - The child's parent pointer is set to NULL
 * - The child continues to exist and can be re-added to another parent
 *
 * The removal maintains the relative order of remaining children by
 * shifting elements after the removed child down by one position.
 *
 * @param parent Pointer to the parent widget. If NULL, does nothing.
 * @param child  Pointer to the child widget to remove. If NULL, does nothing.
 *               If child is not actually a child of parent, does nothing.
 *
 * @note This function does NOT free the child widget. It remains valid
 *       and can be added to another parent or destroyed separately.
 *
 * @note After removal, the child is an orphan with no parent. It will not
 *       be rendered or receive events unless explicitly handled.
 *
 * @see widget_add_child() To add a child to a parent
 * @see widget_destroy() To remove and free a child in one operation
 */
void widget_remove_child(widget_t *parent, widget_t *child) {
    if (!parent || !child)
        return;

    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            // Shift remaining children
            for (int j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            child->parent = NULL;
            break;
        }
    }
}

/**
 * @brief Gets the parent widget of a given widget.
 *
 * This function retrieves the parent in the widget hierarchy. The root
 * widget of a tree has no parent (NULL).
 *
 * @param w Pointer to the widget. If NULL, returns NULL.
 *
 * @return Pointer to the parent widget, or NULL if the widget has no parent
 *         or if w is NULL.
 *
 * @see widget_add_child() Which establishes parent-child relationships
 * @see widget_get_child() To access children from a parent
 */
widget_t *widget_get_parent(widget_t *w) {
    return w ? w->parent : NULL;
}

/**
 * @brief Returns the number of children a widget has.
 *
 * This count can be used to iterate over all children using widget_get_child()
 * with indices from 0 to child_count-1.
 *
 * @param w Pointer to the widget. If NULL, returns 0.
 *
 * @return The number of child widgets, or 0 if w is NULL or has no children.
 *
 * @see widget_get_child() To retrieve a specific child by index
 */
int widget_get_child_count(widget_t *w) {
    return w ? w->child_count : 0;
}

/**
 * @brief Retrieves a child widget by index.
 *
 * Children are ordered by addition order, with index 0 being the first
 * child added (lowest z-order, rendered first) and higher indices being
 * later children (higher z-order, rendered last/on top).
 *
 * @param w     Pointer to the parent widget. If NULL, returns NULL.
 * @param index Zero-based index of the child to retrieve.
 *
 * @return Pointer to the child widget at the given index, or NULL if:
 *         - w is NULL
 *         - index is negative
 *         - index is >= child_count
 *
 * @code
 * // Iterate over all children
 * int count = widget_get_child_count(parent);
 * for (int i = 0; i < count; i++) {
 *     widget_t *child = widget_get_child(parent, i);
 *     // Process child...
 * }
 * @endcode
 *
 * @see widget_get_child_count() To get the total number of children
 */
widget_t *widget_get_child(widget_t *w, int index) {
    if (!w || index < 0 || index >= w->child_count)
        return NULL;
    return w->children[index];
}

/**
 * @brief Marks a widget as needing repaint (placeholder implementation).
 *
 * In a more sophisticated toolkit, this function would mark the widget
 * as "dirty" and schedule a repaint for the next event loop iteration.
 * The current implementation is a placeholder that does nothing.
 *
 * @param w Pointer to the widget that needs repainting. Currently ignored.
 *
 * @note This function currently has no effect. Use widget_app_repaint()
 *       to trigger an immediate full-window repaint instead.
 *
 * @todo Implement proper damage tracking and incremental repaint support.
 *       This would involve maintaining a dirty rectangle and coalescing
 *       multiple repaint requests.
 *
 * @see widget_app_repaint() For a working (but less efficient) repaint
 */
void widget_repaint(widget_t *w) {
    // This is a placeholder - actual repaint needs window context
    (void)w;
}

/**
 * @brief Renders a widget and its descendants to a window.
 *
 * This function is the main entry point for widget rendering. It performs
 * the following steps:
 * 1. Check if the widget is visible; if not, skip rendering entirely
 * 2. Call the widget's custom paint handler (on_paint callback) if set
 * 3. Recursively paint all child widgets via widget_paint_children()
 *
 * The paint order (parent before children) ensures that parent backgrounds
 * are drawn before child content, allowing children to appear "on top" of
 * their parents visually.
 *
 * @param w   Pointer to the widget to render. If NULL, does nothing.
 *            If the widget is not visible, does nothing.
 * @param win Pointer to the GUI window providing the drawing surface.
 *            This is passed to the on_paint callback for use with
 *            gui_fill_rect(), gui_draw_text(), etc.
 *
 * @note Individual widget types (buttons, labels, etc.) register their
 *       paint handlers during creation, so this generic function works
 *       for all widget types.
 *
 * @note The on_paint callback is responsible for drawing the widget's
 *       visual representation. If no callback is set, only children are
 *       rendered (useful for pure container widgets).
 *
 * @see widget_paint_children() Called to render child widgets
 * @see on_paint callback For custom widget rendering
 */
void widget_paint(widget_t *w, gui_window_t *win) {
    if (!w || !w->visible)
        return;

    // Call custom paint handler if set
    if (w->on_paint) {
        w->on_paint(w, win);
    }

    // Paint children
    widget_paint_children(w, win);
}

/**
 * @brief Renders all children of a widget.
 *
 * This function iterates through all child widgets and calls widget_paint()
 * on each one. Children are painted in order of their index (0 first),
 * meaning later children (higher indices) are painted on top of earlier
 * children.
 *
 * @param w   Pointer to the parent widget whose children should be painted.
 *            If NULL, does nothing.
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note This function is typically called by widget_paint() after the parent
 *       has rendered itself, but it can also be called directly if needed.
 *
 * @see widget_paint() The main paint function that calls this
 */
void widget_paint_children(widget_t *w, gui_window_t *win) {
    if (!w)
        return;

    for (int i = 0; i < w->child_count; i++) {
        widget_paint(w->children[i], win);
    }
}

/**
 * @brief Dispatches a mouse event to a widget and its descendants.
 *
 * This function implements the mouse event handling logic with the following
 * characteristics:
 *
 * 1. **Visibility/Enable Check**: Invisible or disabled widgets don't receive
 *    mouse events
 * 2. **Hit Testing**: The event coordinates are checked against the widget's
 *    bounding rectangle; events outside the bounds are ignored
 * 3. **Child-First Dispatch**: Children are checked in reverse order (last
 *    child first = highest z-order) to give topmost widgets priority
 * 4. **Bubbling**: If no child handles the event, the widget itself tries
 *    to handle it via its on_click callback
 *
 * When calling the on_click callback, coordinates are transformed to
 * widget-local space by subtracting the widget's origin.
 *
 * @param w          Pointer to the root widget to dispatch the event to.
 *                   Typically the root widget of the widget tree.
 * @param x          Mouse x-coordinate in window space.
 * @param y          Mouse y-coordinate in window space.
 * @param button     Mouse button identifier (1=left, 2=middle, 3=right).
 * @param event_type Type of mouse event: 1=button down, 2=button up, 3=move.
 *
 * @return True if the event was handled by this widget or a descendant,
 *         false if no widget consumed the event.
 *
 * @note Only button-down events (event_type == 1) trigger the on_click
 *       callback. Button-up and move events are checked for hit testing
 *       but don't invoke the click handler.
 *
 * @note When true is returned, the caller should typically trigger a repaint
 *       as the widget state may have changed.
 *
 * @see on_click callback For handling mouse clicks
 * @see widget_find_at() For hit testing without event dispatch
 */
bool widget_handle_mouse(widget_t *w, int x, int y, int button, int event_type) {
    if (!w || !w->visible || !w->enabled)
        return false;

    // Check if point is inside widget
    if (x < w->x || x >= w->x + w->width || y < w->y || y >= w->y + w->height) {
        return false;
    }

    // Try children first (reverse order for z-order)
    for (int i = w->child_count - 1; i >= 0; i--) {
        if (widget_handle_mouse(w->children[i], x, y, button, event_type)) {
            return true;
        }
    }

    // Handle ourselves
    if (w->on_click && event_type == 1) { // Button down
        w->on_click(w, x - w->x, y - w->y, button);
        return true;
    }

    return false;
}

/**
 * @brief Dispatches a keyboard event to the focused widget.
 *
 * This function searches the widget tree for a focused widget and delivers
 * the keyboard event to it. The search is depth-first, checking the current
 * widget before recursively checking children.
 *
 * The event is delivered by calling the widget's on_key callback with both
 * the raw keycode (for special keys like arrows, function keys) and the
 * translated character (for printable input).
 *
 * @param w       Pointer to the root widget to search. Typically the root
 *                of the widget tree.
 * @param keycode The raw evdev keycode for the key event. This includes
 *                special keys like KEY_BACKSPACE (14), KEY_ENTER (28),
 *                KEY_LEFT (105), etc.
 * @param ch      The ASCII character for the key, or '\0' if the key doesn't
 *                produce a printable character. This is pre-translated by
 *                keycode_to_char() in the caller.
 *
 * @return True if a focused widget was found and handled the event (had an
 *         on_key callback), false if no focused widget was found or if the
 *         focused widget had no key handler.
 *
 * @note Only focused widgets receive keyboard events. Use widget_set_focus()
 *       to direct keyboard input to a specific widget.
 *
 * @note This function only handles key press events. Key release events are
 *       filtered out by the caller (widget_app_run checks event.key.pressed).
 *
 * @see widget_set_focus() To set the focused widget
 * @see on_key callback For handling keyboard input
 * @see keycode_to_char() For keycode translation
 */
bool widget_handle_key(widget_t *w, int keycode, char ch) {
    if (!w || !w->visible || !w->enabled)
        return false;

    // Find focused widget
    if (w->focused && w->on_key) {
        w->on_key(w, keycode, ch);
        return true;
    }

    // Check children
    for (int i = 0; i < w->child_count; i++) {
        if (widget_handle_key(w->children[i], keycode, ch)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Finds the deepest widget at a given screen position.
 *
 * This function performs hit testing on the widget tree to find which widget
 * is visually at a specific point. It returns the most specific (deepest)
 * widget at that location—if a button is inside a panel, clicking on the
 * button returns the button, not the panel.
 *
 * The search uses reverse child order (last child first) to respect z-order:
 * widgets that render on top are found first.
 *
 * @param root The root widget to start searching from. Typically the root
 *             of the widget tree.
 * @param x    The x-coordinate to test (in window space).
 * @param y    The y-coordinate to test (in window space).
 *
 * @return Pointer to the deepest visible widget containing the point, or NULL if:
 *         - root is NULL
 *         - root is not visible
 *         - The point is outside root's bounds
 *
 * @note Invisible widgets and their children are skipped. Disabled widgets
 *       can still be found by this function (unlike mouse event handling).
 *
 * @note If no child contains the point but the root does, the root is returned.
 *
 * @see widget_handle_mouse() Which uses similar logic for event dispatch
 */
widget_t *widget_find_at(widget_t *root, int x, int y) {
    if (!root || !root->visible)
        return NULL;

    // Check if point is inside
    if (x < root->x || x >= root->x + root->width || y < root->y || y >= root->y + root->height) {
        return NULL;
    }

    // Check children (reverse order for z-order)
    for (int i = root->child_count - 1; i >= 0; i--) {
        widget_t *found = widget_find_at(root->children[i], x, y);
        if (found) {
            return found;
        }
    }

    return root;
}

/**
 * @brief Associates arbitrary user data with a widget.
 *
 * This function stores a pointer in the widget that can later be retrieved
 * with widget_get_user_data(). It allows applications to attach custom data
 * to widgets without modifying the widget structure.
 *
 * Common uses include:
 * - Storing application model objects that a widget represents
 * - Keeping track of which item in a list a widget corresponds to
 * - Storing callback context information
 *
 * @param w    Pointer to the widget. If NULL, does nothing.
 * @param data Pointer to user data to store. Can be any pointer value,
 *             including NULL to clear previously stored data.
 *
 * @note The widget does NOT own the user data. The application is responsible
 *       for managing the lifetime of the data and ensuring it remains valid
 *       as long as the widget might access it.
 *
 * @note There is only one user_data slot per widget. Setting new data
 *       replaces any previously stored data.
 *
 * @see widget_get_user_data() To retrieve the stored data
 */
void widget_set_user_data(widget_t *w, void *data) {
    if (w) {
        w->user_data = data;
    }
}

/**
 * @brief Retrieves user data previously associated with a widget.
 *
 * This function returns the pointer stored by a previous call to
 * widget_set_user_data(). If no data was ever stored, returns NULL.
 *
 * @param w Pointer to the widget. If NULL, returns NULL.
 *
 * @return The user data pointer, or NULL if none was set or if w is NULL.
 *
 * @see widget_set_user_data() To store user data
 */
void *widget_get_user_data(widget_t *w) {
    return w ? w->user_data : NULL;
}

//===----------------------------------------------------------------------===//
// Widget Application
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new widget application with a window and event loop.
 *
 * This function initializes the complete widget application framework,
 * providing a ready-to-use window for hosting widgets. It performs:
 *
 * 1. **GUI Initialization**: Calls gui_init() to connect to the display server
 * 2. **Memory Allocation**: Allocates the widget_app_t structure
 * 3. **Window Creation**: Creates a decorated window of the specified size
 * 4. **State Setup**: Initializes the running flag to true
 *
 * After creation, use widget_app_set_root() to set the root widget, then
 * call widget_app_run() to start the event loop.
 *
 * @param title  The window title string (displayed in the title bar).
 *               Should be a short, descriptive name for the application.
 * @param width  The initial window width in pixels (content area, excluding
 *               window decorations).
 * @param height The initial window height in pixels (content area, excluding
 *               window decorations).
 *
 * @return Pointer to the new widget application context, or NULL if:
 *         - GUI initialization failed (display server not available)
 *         - Memory allocation failed
 *         - Window creation failed
 *
 * @note On failure, any partially allocated resources are cleaned up before
 *       returning NULL.
 *
 * @code
 * widget_app_t *app = widget_app_create("My App", 400, 300);
 * if (!app) {
 *     fprintf(stderr, "Failed to create application\n");
 *     return 1;
 * }
 *
 * widget_t *root = widget_create(WIDGET_CONTAINER, NULL);
 * // Add child widgets to root...
 *
 * widget_app_set_root(app, root);
 * widget_app_run(app);
 * widget_app_destroy(app);
 * @endcode
 *
 * @see widget_app_destroy() To clean up the application
 * @see widget_app_set_root() To set the widget hierarchy
 * @see widget_app_run() To start the event loop
 */
widget_app_t *widget_app_create(const char *title, int width, int height) {
    if (gui_init() != 0) {
        return NULL;
    }

    widget_app_t *app = (widget_app_t *)malloc(sizeof(widget_app_t));
    if (!app) {
        gui_shutdown();
        return NULL;
    }

    memset(app, 0, sizeof(widget_app_t));

    app->window = gui_create_window(title, width, height);
    if (!app->window) {
        free(app);
        gui_shutdown();
        return NULL;
    }

    app->running = true;
    return app;
}

/**
 * @brief Destroys a widget application and releases all resources.
 *
 * This function performs complete cleanup of the widget application,
 * in the correct order to avoid resource leaks:
 *
 * 1. **Root Widget**: Destroys the root widget and all its descendants
 * 2. **Active Menu**: Destroys any popup menu that was active
 * 3. **Window**: Destroys the GUI window
 * 4. **GUI Subsystem**: Shuts down the display connection
 * 5. **App Structure**: Frees the widget_app_t structure
 *
 * @param app Pointer to the widget application. If NULL, does nothing.
 *
 * @warning After this call, the app pointer and all pointers to widgets
 *          created within the application become invalid.
 *
 * @warning Do not call this from within the event loop (e.g., from a
 *          button click handler). Use widget_app_quit() instead and let
 *          the event loop exit naturally.
 *
 * @see widget_app_create() To create an application
 * @see widget_app_quit() To request application exit from within handlers
 */
void widget_app_destroy(widget_app_t *app) {
    if (!app)
        return;

    if (app->root) {
        widget_destroy(app->root);
    }

    if (app->active_menu) {
        menu_destroy(app->active_menu);
    }

    if (app->window) {
        gui_destroy_window(app->window);
    }

    gui_shutdown();
    free(app);
}

/**
 * @brief Sets the root widget for the application.
 *
 * The root widget is the top of the widget hierarchy and typically a
 * container widget that holds all other widgets. The root widget's
 * geometry should usually match the window's content area for full
 * coverage.
 *
 * @param app  Pointer to the widget application. If NULL, does nothing.
 * @param root Pointer to the root widget. Should typically be a container
 *             widget with child widgets attached. Can be NULL to clear
 *             the root (though this is unusual).
 *
 * @note The application takes ownership of the root widget. It will be
 *       destroyed when widget_app_destroy() is called.
 *
 * @note If a root was previously set, it is NOT automatically destroyed.
 *       Destroy the old root manually if needed before setting a new one.
 *
 * @see widget_app_create() Returns an app with no root widget
 * @see widget_create() To create the root container
 */
void widget_app_set_root(widget_app_t *app, widget_t *root) {
    if (app) {
        app->root = root;
    }
}

/**
 * @brief Runs the main event loop for the widget application.
 *
 * This function blocks and runs the application's event loop until the
 * application is quit (via widget_app_quit() or window close). It handles:
 *
 * ## Event Processing
 * - **GUI_EVENT_CLOSE**: Sets running=false to exit the loop
 * - **GUI_EVENT_MOUSE**: Dispatches to active menu first, then to widgets
 * - **GUI_EVENT_KEY**: Translates keycode to character and dispatches to
 *   focused widget (only on key press, not release)
 *
 * ## Rendering
 * - Performs initial paint before entering the loop
 * - Repaints after each event that returns "handled"
 *
 * ## CPU Yielding
 * - Executes a yield syscall (SVC #0 with x8=0x0E) after each iteration
 *   to allow other processes to run
 *
 * The function returns when:
 * - The window close button is clicked (GUI_EVENT_CLOSE)
 * - widget_app_quit() is called from an event handler
 *
 * @param app Pointer to the widget application. If NULL, returns immediately.
 *
 * @note This function blocks until the application exits. All event handling
 *       happens within this function.
 *
 * @note Event handlers (on_click, on_key, etc.) are called synchronously
 *       from within this function. Long-running handlers will block the
 *       event loop and make the UI unresponsive.
 *
 * @see widget_app_quit() To exit the event loop programmatically
 * @see widget_app_repaint() Called to refresh the display after events
 */
void widget_app_run(widget_app_t *app) {
    if (!app)
        return;

    // Initial paint
    widget_app_repaint(app);

    while (app->running) {
        gui_event_t event;
        if (gui_poll_event(app->window, &event) == 0) {
            switch (event.type) {
            case GUI_EVENT_CLOSE:
                app->running = false;
                break;

            case GUI_EVENT_MOUSE:
                if (app->active_menu && menu_is_visible(app->active_menu)) {
                    if (menu_handle_mouse(app->active_menu, event.mouse.x, event.mouse.y,
                                          event.mouse.button, event.mouse.event_type)) {
                        widget_app_repaint(app);
                        break;
                    }
                }

                if (app->root) {
                    if (widget_handle_mouse(app->root, event.mouse.x, event.mouse.y,
                                            event.mouse.button, event.mouse.event_type)) {
                        widget_app_repaint(app);
                    }
                }
                break;

            case GUI_EVENT_KEY:
                if (app->root && event.key.pressed) {
                    char ch = keycode_to_char(event.key.keycode, event.key.modifiers);
                    if (widget_handle_key(app->root, event.key.keycode, ch)) {
                        widget_app_repaint(app);
                    }
                }
                break;

            default:
                break;
            }
        }

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }
}

/**
 * @brief Requests the application to exit its event loop.
 *
 * This function sets the application's running flag to false, which causes
 * the event loop in widget_app_run() to exit after completing the current
 * iteration. This is the safe way to exit the application from within
 * event handlers (such as a "Quit" menu item or button click).
 *
 * @param app Pointer to the widget application. If NULL, does nothing.
 *
 * @note This does not immediately exit the application. The current event
 *       handler completes, then the loop checks the running flag and exits.
 *
 * @note After widget_app_run() returns, you should call widget_app_destroy()
 *       to clean up resources.
 *
 * @code
 * // In a button click handler:
 * static void on_quit_clicked(widget_t *w, int x, int y, int button) {
 *     (void)x; (void)y; (void)button;
 *     widget_app_t *app = (widget_app_t *)widget_get_user_data(w);
 *     widget_app_quit(app);
 * }
 * @endcode
 *
 * @see widget_app_run() The event loop that checks the running flag
 */
void widget_app_quit(widget_app_t *app) {
    if (app) {
        app->running = false;
    }
}

/**
 * @brief Redraws the entire application window.
 *
 * This function performs a complete repaint of the application window:
 *
 * 1. **Clear Background**: Fills the entire window with the standard
 *    light gray background color (WB_GRAY_LIGHT)
 * 2. **Paint Widgets**: Renders the root widget and all descendants
 * 3. **Paint Menu**: If a popup menu is active and visible, paints it
 *    on top of the widget content
 * 4. **Present**: Calls gui_present() to flip the buffer and display
 *    the rendered content
 *
 * @param app Pointer to the widget application. If NULL, or if the window
 *            is NULL, does nothing.
 *
 * @note This function repaints the entire window every time. There is no
 *       damage tracking or incremental repaint optimization.
 *
 * @note Menus are painted last to ensure they appear on top of all other
 *       content, regardless of widget z-order.
 *
 * @see widget_paint() The recursive widget rendering function
 * @see gui_present() The buffer flip that makes content visible
 */
void widget_app_repaint(widget_app_t *app) {
    if (!app || !app->window)
        return;

    // Clear background
    int w = gui_get_width(app->window);
    int h = gui_get_height(app->window);
    gui_fill_rect(app->window, 0, 0, w, h, WB_GRAY_LIGHT);

    // Paint widgets
    if (app->root) {
        widget_paint(app->root, app->window);
    }

    // Paint active menu on top
    if (app->active_menu && menu_is_visible(app->active_menu)) {
        menu_paint(app->active_menu, app->window);
    }

    gui_present(app->window);
}
