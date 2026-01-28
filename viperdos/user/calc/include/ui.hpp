#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file ui.hpp
 * @brief Calculator UI rendering and user interaction handling.
 *
 * This file defines the graphical interface for the calculator application,
 * including button layout, color scheme, and input handling. The UI is
 * designed to resemble a classic desktop calculator with 3D-styled buttons.
 *
 * ## Visual Layout
 *
 * ```
 * +-----------------------------+
 * |  [Display Area - "0"]  [M]  |
 * +-----------------------------+
 * | MC | MR | M+ | M- |   C    |
 * +----+----+----+----+---------+
 * | 7  | 8  | 9  | /  |  CE    |
 * +----+----+----+----+---------+
 * | 4  | 5  | 6  | *  | +/-    |
 * +----+----+----+----+---------+
 * | 1  | 2  | 3  | -  | %      |
 * +----+----+----+----+---------+
 * |   0    | .  | +  |   =     |
 * +---------+----+----+---------+
 * ```
 *
 * ## Color Scheme
 *
 * The calculator uses a professional color scheme with distinct button types:
 * - **Digit buttons**: Light gray with dark text (neutral)
 * - **Operator buttons**: Blue with white text (primary action)
 * - **Function buttons**: Dark gray with white text (secondary)
 * - **Clear buttons**: Orange with white text (destructive)
 * - **Display**: White background with black text
 *
 * ## Keyboard Support
 *
 * The calculator accepts both mouse clicks and keyboard input:
 * - Number keys (0-9): Input digits
 * - Period (.): Decimal point
 * - Operators (+, -, *, /): Arithmetic operations
 * - Enter or =: Equals
 * - Escape or C: Clear
 * - Backspace: Clear entry
 *
 * @see calc.hpp for the calculator logic
 */
//===----------------------------------------------------------------------===//

#include "calc.hpp"
#include <gui.h>

namespace calc {
namespace ui {

//===----------------------------------------------------------------------===//
// Color Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup CalcColors Calculator Color Palette
 * @brief Color constants for UI elements.
 *
 * Colors are in ARGB format (0xAARRGGBB) and follow the Amiga Workbench
 * style used throughout ViperDOS applications.
 * @{
 */

/** @brief Window and button face background color (light gray). */
constexpr uint32_t COLOR_BACKGROUND = 0xFFAAAAAA;

/** @brief Display panel background color (white for contrast). */
constexpr uint32_t COLOR_DISPLAY_BG = 0xFFFFFFFF;

/** @brief Display text color (black for readability). */
constexpr uint32_t COLOR_DISPLAY_TEXT = 0xFF000000;

/** @brief Digit button face color (neutral light gray). */
constexpr uint32_t COLOR_BTN_DIGIT = 0xFFAAAAAA;

/** @brief Operator button face color (blue for primary actions). */
constexpr uint32_t COLOR_BTN_OP = 0xFF0055AA;

/** @brief Function button face color (dark gray for secondary actions). */
constexpr uint32_t COLOR_BTN_FUNC = 0xFF888888;

/** @brief Clear button face color (orange for destructive actions). */
constexpr uint32_t COLOR_BTN_CLEAR = 0xFFFF8800;

/** @brief Light text color for buttons with dark backgrounds. */
constexpr uint32_t COLOR_TEXT_LIGHT = 0xFFFFFFFF;

/** @brief Dark text color for buttons with light backgrounds. */
constexpr uint32_t COLOR_TEXT_DARK = 0xFF000000;

/** @brief 3D border highlight color (light edge). */
constexpr uint32_t COLOR_BORDER_LIGHT = 0xFFFFFFFF;

/** @brief 3D border shadow color (dark edge). */
constexpr uint32_t COLOR_BORDER_DARK = 0xFF555555;

/** @brief Memory indicator color (blue, matches operators). */
constexpr uint32_t COLOR_MEMORY = 0xFF0055AA;

/** @} */ // end CalcColors

//===----------------------------------------------------------------------===//
// Dimension Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup CalcDimensions Calculator Layout Dimensions
 * @brief Size and spacing constants for UI layout.
 *
 * These constants define the pixel dimensions of the calculator window
 * and all UI elements. Changing these values will affect the overall
 * appearance and size of the calculator.
 * @{
 */

/** @brief Total window width in pixels. */
constexpr int WIN_WIDTH = 220;

/** @brief Total window height in pixels. */
constexpr int WIN_HEIGHT = 320;

/** @brief Standard button width in pixels. */
constexpr int BTN_WIDTH = 45;

/** @brief Standard button height in pixels. */
constexpr int BTN_HEIGHT = 35;

/** @brief Spacing between buttons in pixels. */
constexpr int BTN_SPACING = 5;

/** @brief Height of the display area in pixels. */
constexpr int DISPLAY_HEIGHT = 50;

/** @brief Margin around the display area in pixels. */
constexpr int DISPLAY_MARGIN = 10;

/** @} */ // end CalcDimensions

//===----------------------------------------------------------------------===//
// Button Types
//===----------------------------------------------------------------------===//

/**
 * @brief Categories of calculator buttons affecting appearance.
 *
 * Each button type has a distinct color scheme to help users quickly
 * identify button functions:
 * - Digits are neutral (easy to find)
 * - Operators stand out (blue)
 * - Functions are muted (gray)
 * - Clear is alarming (orange)
 */
enum class ButtonType {
    Digit,    /**< Number buttons (0-9, decimal point). */
    Operator, /**< Arithmetic operator buttons (+, -, *, /, =). */
    Function, /**< Function buttons (%, +/-, 1/x). */
    Clear     /**< Clear buttons (C, CE). */
};

/**
 * @brief Definition of a calculator button.
 *
 * Each button is positioned in a grid layout and has associated
 * label text, action code, and visual type.
 *
 * ## Grid Layout
 *
 * Buttons are arranged in a 5-column grid below the display:
 * - **row**: 0-based row index (0 = top row of buttons)
 * - **col**: 0-based column index (0 = leftmost)
 * - **colSpan**: Number of columns the button occupies (1-2)
 *
 * ## Action Codes
 *
 * Each button has a single-character action code used by the
 * main application loop to dispatch to the appropriate handler:
 * - '0'-'9': Digits
 * - '.': Decimal point
 * - '+', '-', '*', '/': Operators
 * - '=': Equals
 * - 'C': Clear, 'E': Clear entry
 * - 'N': Negate, '%': Percent, 'I': Inverse
 * - 'M': Memory clear, 'R': Memory recall, 'P': Memory plus
 */
struct Button {
    int row;           /**< Grid row index (0 = first button row). */
    int col;           /**< Grid column index (0 = leftmost). */
    int colSpan;       /**< Number of columns occupied (usually 1). */
    const char *label; /**< Button label text for display. */
    char action;       /**< Action character for input dispatch. */
    ButtonType type;   /**< Visual category for coloring. */
};

//===----------------------------------------------------------------------===//
// UI Functions
//===----------------------------------------------------------------------===//

/**
 * @brief Determines which button is at a given screen position.
 *
 * Performs hit-testing to find which button, if any, contains the
 * specified screen coordinates. This is used for mouse click handling.
 *
 * @param x The X coordinate in window-local pixels.
 * @param y The Y coordinate in window-local pixels.
 *
 * @return The action character of the button at that position, or 0 if
 *         no button was clicked.
 *
 * @note Coordinates are relative to the window content area (excluding
 *       the title bar and borders managed by the window manager).
 */
char getButtonAt(int x, int y);

/**
 * @brief Renders the complete calculator interface.
 *
 * Draws all calculator UI elements to the window's pixel buffer:
 * 1. Background fill
 * 2. Display area with current value
 * 3. Memory indicator (if memory has a value)
 * 4. All buttons with 3D styling
 *
 * The function automatically calls gui_present() to show the
 * updated display.
 *
 * @param win   Pointer to the GUI window to render to.
 * @param state Current calculator state (for display value and memory).
 *
 * @note Call this function after any state change that affects the display.
 */
void render(gui_window_t *win, const State &state);

/**
 * @brief Converts a keyboard input to a calculator action.
 *
 * Maps keyboard key codes and modifiers to calculator action characters.
 * This enables full keyboard control of the calculator without requiring
 * mouse input.
 *
 * ## Key Mappings
 *
 * | Key(s)          | Action |
 * |-----------------|--------|
 * | 0-9             | '0'-'9' (digits) |
 * | .               | '.' (decimal) |
 * | +, -, *, /      | operators |
 * | Enter or =      | '=' (equals) |
 * | Escape or C     | 'C' (clear) |
 * | Backspace       | 'E' (clear entry) |
 *
 * @param keycode   The HID keycode from the keyboard event.
 * @param modifiers Modifier key flags (shift, ctrl, etc.).
 *
 * @return The action character for the pressed key, or 0 if the key
 *         doesn't map to any calculator action.
 */
char keyToAction(uint16_t keycode, uint8_t modifiers);

} // namespace ui
} // namespace calc
