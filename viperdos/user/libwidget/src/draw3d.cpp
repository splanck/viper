//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file draw3d.c
 * @brief Amiga Workbench-style 3D drawing primitives for the libwidget toolkit.
 *
 * This file provides low-level 3D drawing functions that create the classic
 * beveled appearance of Amiga Workbench 3.x UI elements. All libwidget widgets
 * use these functions for their visual rendering.
 *
 * ## 3D Effect Technique
 *
 * The 3D illusion is achieved through strategic use of light and shadow colors
 * along element edges. The visual metaphor assumes a light source from the
 * top-left corner:
 *
 * - **Raised elements** (buttons, panels): Light color on top and left edges,
 *   shadow color on bottom and right edges. Creates the appearance of a
 *   surface projecting outward from the screen.
 *
 * - **Sunken elements** (text fields, insets): Shadow color on top and left,
 *   light color on bottom and right. Creates the appearance of a recessed
 *   area cut into the surface.
 *
 * ## Standard Colors
 *
 * The Amiga Workbench look uses a specific color palette:
 * - **WB_GRAY_LIGHT (0xFFAAAAAA)**: The main surface color, used as face color
 * - **WB_WHITE (0xFFFFFFFF)**: Highlight color for illuminated edges
 * - **WB_GRAY_DARK (0xFF555555)**: Shadow color for shaded edges
 *
 * These colors create a neutral gray appearance that works well with both
 * the blue desktop background and any content displayed on the widgets.
 *
 * ## Drawing Order
 *
 * When drawing 3D elements, the order of operations matters:
 * 1. Fill the face (background) first
 * 2. Draw light edges (top and left for raised, bottom and right for sunken)
 * 3. Draw shadow edges (opposite edges)
 * 4. Draw any additional inner bevels if needed
 *
 * @see widget.h for color constants and function declarations
 * @see Individual widget files for usage examples
 */
//===----------------------------------------------------------------------===//

#include <widget.h>

//===----------------------------------------------------------------------===//
// 3D Drawing Functions
//===----------------------------------------------------------------------===//

/**
 * @brief Draws a raised 3D rectangle (appears to project outward from screen).
 *
 * This function creates the visual effect of a surface that projects toward
 * the viewer. The top and left edges are drawn in a light color (simulating
 * illumination from a top-left light source), while the bottom and right
 * edges are drawn in a shadow color.
 *
 * The rendering process:
 * 1. Fill the entire rectangle with the face color
 * 2. Draw the top edge with the light color
 * 3. Draw the left edge with the light color
 * 4. Draw the bottom edge with the shadow color
 * 5. Draw the right edge with the shadow color
 *
 * ## Visual Effect
 *
 * ```
 * LLLLLLLLLLLS
 * L          S
 * L   FACE   S
 * L          S
 * SSSSSSSSSSS
 *
 * L = light color, S = shadow color
 * ```
 *
 * @param win    Pointer to the GUI window for drawing operations.
 * @param x      X coordinate of the rectangle's top-left corner.
 * @param y      Y coordinate of the rectangle's top-left corner.
 * @param w      Width of the rectangle in pixels.
 * @param h      Height of the rectangle in pixels.
 * @param face   Color to fill the interior of the rectangle.
 * @param light  Color for the top and left edges (typically WB_WHITE).
 * @param shadow Color for the bottom and right edges (typically WB_GRAY_DARK).
 *
 * @note The edge lines are drawn on top of the filled rectangle, so the
 *       bevel consumes 1 pixel on each edge of the specified dimensions.
 *
 * @see draw_3d_sunken() For the opposite visual effect
 * @see draw_3d_button() For complete button rendering with inner bevels
 */
void draw_3d_raised(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow) {
    // Fill face
    gui_fill_rect(win, x, y, w, h, face);

    // Top edge (light)
    gui_draw_hline(win, x, x + w - 1, y, light);

    // Left edge (light)
    gui_draw_vline(win, x, y, y + h - 1, light);

    // Bottom edge (shadow)
    gui_draw_hline(win, x, x + w - 1, y + h - 1, shadow);

    // Right edge (shadow)
    gui_draw_vline(win, x + w - 1, y, y + h - 1, shadow);
}

/**
 * @brief Draws a sunken 3D rectangle (appears recessed into the screen).
 *
 * This function creates the visual effect of a surface that is cut into
 * the screen, receding away from the viewer. The colors are inverted from
 * draw_3d_raised(): top and left edges are shadowed (appear in darkness),
 * while bottom and right edges are lit (catching light from above).
 *
 * The rendering process:
 * 1. Fill the entire rectangle with the face color
 * 2. Draw the top edge with the shadow color
 * 3. Draw the left edge with the shadow color
 * 4. Draw the bottom edge with the light color
 * 5. Draw the right edge with the light color
 *
 * ## Visual Effect
 *
 * ```
 * SSSSSSSSSSSL
 * S          L
 * S   FACE   L
 * S          L
 * LLLLLLLLLLLL
 *
 * L = light color, S = shadow color
 * ```
 *
 * Common uses for sunken rectangles:
 * - Text input fields (textbox backgrounds)
 * - Checkbox boxes
 * - Inset panels
 * - List view backgrounds
 *
 * @param win    Pointer to the GUI window for drawing operations.
 * @param x      X coordinate of the rectangle's top-left corner.
 * @param y      Y coordinate of the rectangle's top-left corner.
 * @param w      Width of the rectangle in pixels.
 * @param h      Height of the rectangle in pixels.
 * @param face   Color to fill the interior (typically WB_WHITE for inputs).
 * @param light  Color for the bottom and right edges (typically WB_WHITE).
 * @param shadow Color for the top and left edges (typically WB_GRAY_DARK).
 *
 * @note The face color for sunken elements is often white (WB_WHITE) to
 *       provide contrast for text input, unlike raised elements which
 *       typically use gray (WB_GRAY_LIGHT).
 *
 * @see draw_3d_raised() For the opposite visual effect
 */
void draw_3d_sunken(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow) {
    // Fill face
    gui_fill_rect(win, x, y, w, h, face);

    // Top edge (shadow)
    gui_draw_hline(win, x, x + w - 1, y, shadow);

    // Left edge (shadow)
    gui_draw_vline(win, x, y, y + h - 1, shadow);

    // Bottom edge (light)
    gui_draw_hline(win, x, x + w - 1, y + h - 1, light);

    // Right edge (light)
    gui_draw_vline(win, x + w - 1, y, y + h - 1, light);
}

/**
 * @brief Draws a complete 3D button with enhanced beveling.
 *
 * This function renders a push button with authentic Amiga Workbench styling.
 * Unlike the simpler draw_3d_raised() and draw_3d_sunken(), this function
 * adds an inner bevel for enhanced depth perception:
 *
 * ## Normal (Unpressed) State
 * - Outer edges: raised bevel (light top/left, shadow bottom/right)
 * - Inner highlight: additional white line inside top and left
 * - Creates a prominent "pop out" effect
 *
 * ## Pressed State
 * - Outer edges: sunken bevel (shadow top/left, light bottom/right)
 * - Inner shadow: additional dark line inside top and left
 * - Creates an "pushed in" effect
 *
 * The additional inner bevel makes buttons feel more tactile and provides
 * stronger visual feedback when pressed.
 *
 * @param win     Pointer to the GUI window for drawing operations.
 * @param x       X coordinate of the button's top-left corner.
 * @param y       Y coordinate of the button's top-left corner.
 * @param w       Width of the button in pixels.
 * @param h       Height of the button in pixels.
 * @param pressed True for pressed/sunken appearance, false for normal/raised.
 *
 * @note This function uses fixed colors (WB_GRAY_LIGHT for face, WB_WHITE
 *       for highlights, WB_GRAY_DARK for shadows) to match the Workbench style.
 *
 * @note The button face color is always WB_GRAY_LIGHT, regardless of pressed
 *       state. Only the bevel direction changes.
 *
 * @see button_paint() Which uses this for button widget rendering
 */
void draw_3d_button(gui_window_t *win, int x, int y, int w, int h, bool pressed) {
    if (pressed) {
        // Pressed state - sunken
        draw_3d_sunken(win, x, y, w, h, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

        // Inner shadow for more depth
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_GRAY_DARK);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_GRAY_DARK);
    } else {
        // Normal state - raised
        draw_3d_raised(win, x, y, w, h, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

        // Extra highlight for Amiga look
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_WHITE);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_WHITE);
    }
}

/**
 * @brief Draws a double-line 3D frame border (raised or sunken).
 *
 * This function creates a thick 3D border consisting of two concentric
 * beveled lines. This creates a stronger visual boundary than the single
 * bevel of draw_3d_raised/sunken, and is typically used for:
 * - Group boxes around related controls
 * - Panel borders
 * - Dialog outlines
 * - Separator frames
 *
 * The frame is purely decorative—it draws only the border lines without
 * filling any interior area.
 *
 * ## Sunken Frame Structure (2 pixels wide)
 * ```
 * Outer: shadow on top-left, light on bottom-right
 * Inner: light on top-left, shadow on bottom-right
 * Creates a "chiseled groove" appearance
 * ```
 *
 * ## Raised Frame Structure (2 pixels wide)
 * ```
 * Outer: light on top-left, shadow on bottom-right
 * Inner: shadow on top-left, light on bottom-right
 * Creates a "protruding ridge" appearance
 * ```
 *
 * @param win    Pointer to the GUI window for drawing operations.
 * @param x      X coordinate of the frame's top-left corner.
 * @param y      Y coordinate of the frame's top-left corner.
 * @param w      Width of the framed area in pixels.
 * @param h      Height of the framed area in pixels.
 * @param sunken True for sunken/groove appearance, false for raised/ridge.
 *
 * @note The frame consumes 2 pixels on each edge. The interior area starts
 *       at (x+2, y+2) and has dimensions (w-4, h-4).
 *
 * @note Unlike draw_3d_button(), this function does not fill the interior.
 *       Use gui_fill_rect() separately if you need a filled background.
 *
 * @see draw_3d_groove() For a simpler single-line separator
 */
void draw_3d_frame(gui_window_t *win, int x, int y, int w, int h, bool sunken) {
    if (sunken) {
        // Outer shadow
        gui_draw_hline(win, x, x + w - 1, y, WB_GRAY_DARK);
        gui_draw_vline(win, x, y, y + h - 1, WB_GRAY_DARK);

        // Outer light
        gui_draw_hline(win, x + 1, x + w - 1, y + h - 1, WB_WHITE);
        gui_draw_vline(win, x + w - 1, y + 1, y + h - 1, WB_WHITE);

        // Inner light
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_WHITE);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_WHITE);

        // Inner shadow
        gui_draw_hline(win, x + 2, x + w - 2, y + h - 2, WB_GRAY_DARK);
        gui_draw_vline(win, x + w - 2, y + 2, y + h - 2, WB_GRAY_DARK);
    } else {
        // Outer light
        gui_draw_hline(win, x, x + w - 1, y, WB_WHITE);
        gui_draw_vline(win, x, y, y + h - 1, WB_WHITE);

        // Outer shadow
        gui_draw_hline(win, x + 1, x + w - 1, y + h - 1, WB_GRAY_DARK);
        gui_draw_vline(win, x + w - 1, y + 1, y + h - 1, WB_GRAY_DARK);

        // Inner shadow
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_GRAY_DARK);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_GRAY_DARK);

        // Inner light
        gui_draw_hline(win, x + 2, x + w - 2, y + h - 2, WB_WHITE);
        gui_draw_vline(win, x + w - 2, y + 2, y + h - 2, WB_WHITE);
    }
}

/**
 * @brief Draws a 3D groove separator line (horizontal or vertical).
 *
 * This function creates a decorative separator line with 3D appearance,
 * consisting of two parallel lines (dark then light) that create the
 * illusion of a groove or channel cut into the surface.
 *
 * The orientation is automatically determined by the aspect ratio:
 * - **Horizontal groove** (when w > h): Two horizontal lines, dark on top
 * - **Vertical groove** (when h >= w): Two vertical lines, dark on left
 *
 * Common uses:
 * - Separating groups of controls in toolbars
 * - Dividing sections of a dialog
 * - Creating visual breaks between unrelated UI elements
 *
 * @param win Pointer to the GUI window for drawing operations.
 * @param x   X coordinate of the groove area.
 * @param y   Y coordinate of the groove area.
 * @param w   Width of the groove area. For horizontal grooves, this is the
 *            length of the line.
 * @param h   Height of the groove area. For vertical grooves, this is the
 *            length of the line.
 *
 * @note The groove is drawn centered within the specified rectangle:
 *       - Horizontal: at vertical center (y + h/2)
 *       - Vertical: at horizontal center (x + w/2)
 *
 * @note The groove always consists of exactly 2 lines (2 pixels thick):
 *       one dark (WB_GRAY_DARK) and one light (WB_WHITE).
 *
 * @code
 * // Create a horizontal separator below a toolbar
 * draw_3d_groove(win, 10, 30, toolbar_width - 20, 4);
 *
 * // Create a vertical separator between icon groups
 * draw_3d_groove(win, 100, 5, 4, toolbar_height - 10);
 * @endcode
 *
 * @see draw_3d_frame() For a rectangular groove border
 */
void draw_3d_groove(gui_window_t *win, int x, int y, int w, int h) {
    // Horizontal groove
    if (w > h) {
        int cy = y + h / 2;
        gui_draw_hline(win, x, x + w - 1, cy, WB_GRAY_DARK);
        gui_draw_hline(win, x, x + w - 1, cy + 1, WB_WHITE);
    }
    // Vertical groove
    else {
        int cx = x + w / 2;
        gui_draw_vline(win, cx, y, y + h - 1, WB_GRAY_DARK);
        gui_draw_vline(win, cx + 1, y, y + h - 1, WB_WHITE);
    }
}
