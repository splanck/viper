//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "keymap.hpp"

namespace consoled {

char keycode_to_ascii(uint16_t keycode, uint8_t modifiers) {
    bool shift = (modifiers & MOD_SHIFT) != 0;
    bool ctrl = (modifiers & MOD_CTRL) != 0;

    // Letters - top row
    if (keycode >= KEY_Q && keycode <= KEY_P) {
        static const char row1[] = "qwertyuiop";
        char c = row1[keycode - KEY_Q];
        // Handle Ctrl+letter -> control character (Ctrl+A=1, Ctrl+Q=17, etc.)
        if (ctrl)
            return static_cast<char>((c - 'a') + 1);
        return shift ? static_cast<char>(c - 32) : c;
    }

    // Letters - home row
    if (keycode >= KEY_A && keycode <= KEY_L) {
        static const char row2[] = "asdfghjkl";
        char c = row2[keycode - KEY_A];
        if (ctrl)
            return static_cast<char>((c - 'a') + 1);
        return shift ? static_cast<char>(c - 32) : c;
    }

    // Letters - bottom row
    if (keycode >= KEY_Z && keycode <= KEY_M) {
        static const char row3[] = "zxcvbnm";
        char c = row3[keycode - KEY_Z];
        if (ctrl)
            return static_cast<char>((c - 'a') + 1);
        return shift ? static_cast<char>(c - 32) : c;
    }

    // Numbers
    if (keycode >= KEY_1 && keycode <= KEY_0) {
        static const char nums[] = "1234567890";
        static const char syms[] = "!@#$%^&*()";
        int idx = (keycode == KEY_0) ? 9 : (keycode - KEY_1);
        return shift ? syms[idx] : nums[idx];
    }

    // Special keys
    switch (keycode) {
        case KEY_SPACE:
            return ' ';
        case KEY_ENTER:
            return '\r';
        case KEY_BACKSPACE:
            return '\b';
        case KEY_TAB:
            return '\t';
        case KEY_ESC:
            return 27;
        case KEY_MINUS:
            return shift ? '_' : '-';
        case KEY_EQUAL:
            return shift ? '+' : '=';
        case KEY_LEFTBRACE:
            return shift ? '{' : '[';
        case KEY_RIGHTBRACE:
            return shift ? '}' : ']';
        case KEY_SEMICOLON:
            return shift ? ':' : ';';
        case KEY_APOSTROPHE:
            return shift ? '"' : '\'';
        case KEY_GRAVE:
            return shift ? '~' : '`';
        case KEY_BACKSLASH:
            return shift ? '|' : '\\';
        case KEY_COMMA:
            return shift ? '<' : ',';
        case KEY_DOT:
            return shift ? '>' : '.';
        case KEY_SLASH:
            return shift ? '?' : '/';
    }

    return 0; // Unknown or special key
}

} // namespace consoled
