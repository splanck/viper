#pragma once
//===----------------------------------------------------------------------===//
// Calculator UI rendering and interaction
//===----------------------------------------------------------------------===//

#include "calc.hpp"
#include <gui.h>

namespace calc {
namespace ui {

// Colors
constexpr uint32_t COLOR_BACKGROUND = 0xFFAAAAAA;
constexpr uint32_t COLOR_DISPLAY_BG = 0xFFFFFFFF;
constexpr uint32_t COLOR_DISPLAY_TEXT = 0xFF000000;
constexpr uint32_t COLOR_BTN_DIGIT = 0xFFAAAAAA;
constexpr uint32_t COLOR_BTN_OP = 0xFF0055AA;
constexpr uint32_t COLOR_BTN_FUNC = 0xFF888888;
constexpr uint32_t COLOR_BTN_CLEAR = 0xFFFF8800;
constexpr uint32_t COLOR_TEXT_LIGHT = 0xFFFFFFFF;
constexpr uint32_t COLOR_TEXT_DARK = 0xFF000000;
constexpr uint32_t COLOR_BORDER_LIGHT = 0xFFFFFFFF;
constexpr uint32_t COLOR_BORDER_DARK = 0xFF555555;
constexpr uint32_t COLOR_MEMORY = 0xFF0055AA;

// Dimensions
constexpr int WIN_WIDTH = 220;
constexpr int WIN_HEIGHT = 320;
constexpr int BTN_WIDTH = 45;
constexpr int BTN_HEIGHT = 35;
constexpr int BTN_SPACING = 5;
constexpr int DISPLAY_HEIGHT = 50;
constexpr int DISPLAY_MARGIN = 10;

// Button types
enum class ButtonType { Digit, Operator, Function, Clear };

// Button definition
struct Button {
    int row, col;
    int colSpan;
    const char *label;
    char action;
    ButtonType type;
};

// Get button at screen position, returns action char or 0
char getButtonAt(int x, int y);

// Render the calculator
void render(gui_window_t *win, const State &state);

// Handle keyboard input, returns action char or 0
char keyToAction(uint16_t keycode, uint8_t modifiers);

} // namespace ui
} // namespace calc
