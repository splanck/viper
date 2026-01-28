//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file ui.cpp
 * @brief Calculator UI rendering and input handling.
 *
 * This file implements the calculator's graphical user interface, including:
 * - Button layout and rendering with 3D effects
 * - Display area with sunken frame appearance
 * - Mouse click hit-testing
 * - Keyboard input translation
 *
 * ## Button Layout
 *
 * The calculator has a 6x4 grid of buttons:
 * ```
 * +----+----+----+----+
 * | MC | MR | M+ | C  |  Row 0: Memory and Clear
 * +----+----+----+----+
 * | 7  | 8  | 9  | /  |  Row 1: 7-8-9 and divide
 * +----+----+----+----+
 * | 4  | 5  | 6  | *  |  Row 2: 4-5-6 and multiply
 * +----+----+----+----+
 * | 1  | 2  | 3  | -  |  Row 3: 1-2-3 and subtract
 * +----+----+----+----+
 * | 0  | .  | =  | +  |  Row 4: 0, decimal, equals, add
 * +----+----+----+----+
 * |+/- | CE | %  |1/x |  Row 5: Special functions
 * +----+----+----+----+
 * ```
 *
 * ## Button Types and Colors
 *
 * | Type     | Color  | Examples            |
 * |----------|--------|---------------------|
 * | Digit    | White  | 0-9, decimal point  |
 * | Operator | Orange | +, -, *, /, =       |
 * | Function | Gray   | MC, MR, M+, +/-     |
 * | Clear    | Red    | C, CE               |
 *
 * ## Keyboard Mapping
 *
 * The calculator accepts keyboard input via evdev keycodes:
 * - Number row (1-9, 0) and numpad for digits
 * - Numpad operators (+, -, *, /)
 * - Enter or numpad Enter for equals
 * - Escape for clear (C)
 * - Backspace for clear entry (CE)
 *
 * @see ui.hpp for Button structure and constants
 * @see calc.hpp for calculator state
 */
//===----------------------------------------------------------------------===//

#include "../include/ui.hpp"
#include <string.h>

namespace calc {
namespace ui {

// Button layout
static const Button g_buttons[] = {
    // Row 0: Memory and Clear
    {0, 0, 1, "MC", 'M', ButtonType::Function},
    {0, 1, 1, "MR", 'R', ButtonType::Function},
    {0, 2, 1, "M+", 'P', ButtonType::Function},
    {0, 3, 1, "C", 'C', ButtonType::Clear},

    // Row 1: 7, 8, 9, /
    {1, 0, 1, "7", '7', ButtonType::Digit},
    {1, 1, 1, "8", '8', ButtonType::Digit},
    {1, 2, 1, "9", '9', ButtonType::Digit},
    {1, 3, 1, "/", '/', ButtonType::Operator},

    // Row 2: 4, 5, 6, *
    {2, 0, 1, "4", '4', ButtonType::Digit},
    {2, 1, 1, "5", '5', ButtonType::Digit},
    {2, 2, 1, "6", '6', ButtonType::Digit},
    {2, 3, 1, "*", '*', ButtonType::Operator},

    // Row 3: 1, 2, 3, -
    {3, 0, 1, "1", '1', ButtonType::Digit},
    {3, 1, 1, "2", '2', ButtonType::Digit},
    {3, 2, 1, "3", '3', ButtonType::Digit},
    {3, 3, 1, "-", '-', ButtonType::Operator},

    // Row 4: 0, ., =, +
    {4, 0, 1, "0", '0', ButtonType::Digit},
    {4, 1, 1, ".", '.', ButtonType::Digit},
    {4, 2, 1, "=", '=', ButtonType::Clear},
    {4, 3, 1, "+", '+', ButtonType::Operator},

    // Row 5: +/-, CE, %, 1/x
    {5, 0, 1, "+/-", 'N', ButtonType::Function},
    {5, 1, 1, "CE", 'E', ButtonType::Clear},
    {5, 2, 1, "%", '%', ButtonType::Operator},
    {5, 3, 1, "1/x", 'I', ButtonType::Operator},
};
constexpr int NUM_BUTTONS = sizeof(g_buttons) / sizeof(g_buttons[0]);

static void getButtonRect(const Button &btn, int &x, int &y, int &w, int &h) {
    x = DISPLAY_MARGIN + btn.col * (BTN_WIDTH + BTN_SPACING);
    y = DISPLAY_MARGIN + DISPLAY_HEIGHT + 10 + btn.row * (BTN_HEIGHT + BTN_SPACING);
    w = BTN_WIDTH * btn.colSpan + BTN_SPACING * (btn.colSpan - 1);
    h = BTN_HEIGHT;
}

static uint32_t getButtonColor(ButtonType type) {
    switch (type) {
        case ButtonType::Digit:
            return COLOR_BTN_DIGIT;
        case ButtonType::Operator:
            return COLOR_BTN_OP;
        case ButtonType::Function:
            return COLOR_BTN_FUNC;
        case ButtonType::Clear:
            return COLOR_BTN_CLEAR;
        default:
            return COLOR_BTN_DIGIT;
    }
}

static uint32_t getButtonTextColor(ButtonType type) {
    switch (type) {
        case ButtonType::Digit:
            return COLOR_TEXT_DARK;
        case ButtonType::Operator:
        case ButtonType::Clear:
            return COLOR_TEXT_LIGHT;
        case ButtonType::Function:
            return COLOR_TEXT_LIGHT;
        default:
            return COLOR_TEXT_DARK;
    }
}

static void drawButton(gui_window_t *win, const Button &btn, bool pressed) {
    int x, y, w, h;
    getButtonRect(btn, x, y, w, h);

    uint32_t color = getButtonColor(btn.type);

    // Fill
    gui_fill_rect(win, x, y, w, h, color);

    // 3D border
    if (pressed) {
        gui_draw_hline(win, x, x + w - 1, y, COLOR_BORDER_DARK);
        gui_draw_vline(win, x, y, y + h - 1, COLOR_BORDER_DARK);
        gui_draw_hline(win, x, x + w - 1, y + h - 1, COLOR_BORDER_LIGHT);
        gui_draw_vline(win, x + w - 1, y, y + h - 1, COLOR_BORDER_LIGHT);
    } else {
        gui_draw_hline(win, x, x + w - 1, y, COLOR_BORDER_LIGHT);
        gui_draw_vline(win, x, y, y + h - 1, COLOR_BORDER_LIGHT);
        gui_draw_hline(win, x, x + w - 1, y + h - 1, COLOR_BORDER_DARK);
        gui_draw_vline(win, x + w - 1, y, y + h - 1, COLOR_BORDER_DARK);
    }

    // Label
    int labelLen = static_cast<int>(strlen(btn.label));
    int labelX = x + (w - labelLen * 8) / 2;
    int labelY = y + (h - 10) / 2;
    gui_draw_text(win, labelX, labelY, btn.label, getButtonTextColor(btn.type));
}

static void drawDisplay(gui_window_t *win, const State &state) {
    int x = DISPLAY_MARGIN;
    int y = DISPLAY_MARGIN;
    int w = WIN_WIDTH - 2 * DISPLAY_MARGIN;
    int h = DISPLAY_HEIGHT;

    // Sunken frame
    gui_fill_rect(win, x, y, w, h, COLOR_DISPLAY_BG);
    gui_draw_hline(win, x, x + w - 1, y, COLOR_BORDER_DARK);
    gui_draw_vline(win, x, y, y + h - 1, COLOR_BORDER_DARK);
    gui_draw_hline(win, x, x + w - 1, y + h - 1, COLOR_BORDER_LIGHT);
    gui_draw_vline(win, x + w - 1, y, y + h - 1, COLOR_BORDER_LIGHT);

    // Display text (right-aligned)
    int textLen = static_cast<int>(strlen(state.display));
    int textX = x + w - 10 - textLen * 8;
    int textY = y + (h - 10) / 2;
    gui_draw_text(win, textX, textY, state.display, COLOR_DISPLAY_TEXT);

    // Memory indicator
    if (state.hasMemory) {
        gui_draw_text(win, x + 5, y + 5, "M", COLOR_MEMORY);
    }
}

char getButtonAt(int x, int y) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        int bx, by, bw, bh;
        getButtonRect(g_buttons[i], bx, by, bw, bh);

        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            return g_buttons[i].action;
        }
    }
    return 0;
}

void render(gui_window_t *win, const State &state) {
    // Background
    gui_fill_rect(win, 0, 0, WIN_WIDTH, WIN_HEIGHT, COLOR_BACKGROUND);

    // Display
    drawDisplay(win, state);

    // Buttons
    for (int i = 0; i < NUM_BUTTONS; i++) {
        drawButton(win, g_buttons[i], false);
    }

    gui_present(win);
}

char keyToAction(uint16_t keycode, uint8_t modifiers) {
    bool shift = (modifiers & 1) != 0;

    // Number keys (evdev: 1=2, 2=3, ..., 9=10, 0=11)
    if (keycode >= 2 && keycode <= 10) {
        return '1' + (keycode - 2);
    }
    if (keycode == 11) {
        return '0';
    }

    // Numpad digits (evdev: KP_0=82, KP_1=79, etc.)
    if (keycode >= 79 && keycode <= 81) {
        return '1' + (keycode - 79);
    }
    if (keycode >= 75 && keycode <= 77) {
        return '4' + (keycode - 75);
    }
    if (keycode >= 71 && keycode <= 73) {
        return '7' + (keycode - 71);
    }
    if (keycode == 82) {
        return '0';
    }

    // Operators
    if (keycode == 78) { // KP_+
        return '+';
    }
    if (keycode == 74) { // KP_-
        return '-';
    }
    if (keycode == 55) { // KP_*
        return '*';
    }
    if (keycode == 98) { // KP_/
        return '/';
    }
    if (keycode == 83) { // KP_.
        return '.';
    }
    if (keycode == 96) { // KP_Enter
        return '=';
    }

    // Main keyboard operators (with shift)
    if (keycode == 13 && shift) { // + (shift =)
        return '+';
    }
    if (keycode == 12 && !shift) { // -
        return '-';
    }
    if (keycode == 9 && shift) { // * (shift 8)
        return '*';
    }
    if (keycode == 53 && !shift) { // /
        return '/';
    }
    if (keycode == 52 && !shift) { // .
        return '.';
    }
    if (keycode == 13 && !shift) { // =
        return '=';
    }

    // Special keys
    if (keycode == 28) { // Enter
        return '=';
    }
    if (keycode == 1) { // Escape
        return 'C';
    }
    if (keycode == 14) { // Backspace
        return 'E';
    }

    return 0;
}

} // namespace ui
} // namespace calc
