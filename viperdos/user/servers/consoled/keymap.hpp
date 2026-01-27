//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

namespace consoled {

// =============================================================================
// Linux evdev Keycodes
// =============================================================================

enum KeyCode : uint16_t {
    KEY_ESC = 1,
    KEY_1 = 2,
    KEY_0 = 11,
    KEY_MINUS = 12,
    KEY_EQUAL = 13,
    KEY_BACKSPACE = 14,
    KEY_TAB = 15,
    KEY_Q = 16,
    KEY_P = 25,
    KEY_LEFTBRACE = 26,
    KEY_RIGHTBRACE = 27,
    KEY_ENTER = 28,
    KEY_A = 30,
    KEY_L = 38,
    KEY_SEMICOLON = 39,
    KEY_APOSTROPHE = 40,
    KEY_GRAVE = 41,
    KEY_BACKSLASH = 43,
    KEY_Z = 44,
    KEY_M = 50,
    KEY_COMMA = 51,
    KEY_DOT = 52,
    KEY_SLASH = 53,
    KEY_SPACE = 57,
};

// =============================================================================
// Modifier Flags
// =============================================================================

constexpr uint8_t MOD_SHIFT = 0x01;
constexpr uint8_t MOD_CTRL  = 0x02;
constexpr uint8_t MOD_ALT   = 0x04;

// =============================================================================
// Keymap Functions
// =============================================================================

/**
 * @brief Convert a keycode to ASCII character.
 *
 * @param keycode Linux evdev keycode
 * @param modifiers Modifier flags (shift, ctrl, alt)
 * @return ASCII character, or 0 for unknown/special keys
 */
char keycode_to_ascii(uint16_t keycode, uint8_t modifiers);

} // namespace consoled
