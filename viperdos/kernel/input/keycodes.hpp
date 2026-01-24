#pragma once

#include "../include/types.hpp"

/**
 * @file keycodes.hpp
 * @brief Linux evdev key codes and translation helpers.
 *
 * @details
 * QEMU's virtio-keyboard device reports key events using Linux evdev key codes
 * (as defined in `linux/input-event-codes.h`). This header defines the subset
 * of key codes used by the kernel and declares helpers for:
 * - Translating key codes into ASCII characters given modifier state.
 * - Determining whether a key code represents a modifier key.
 * - Mapping modifier key codes to modifier mask bits.
 */
namespace input {

// Linux evdev keycodes (from linux/input-event-codes.h)
// These are what QEMU's virtio-keyboard sends
/**
 * @brief Constants for Linux evdev key codes.
 *
 * @details
 * The values are chosen to match the evdev codes used by virtio input so that
 * the input subsystem can interpret raw key events.
 */
namespace key {
constexpr u16 NONE = 0;

// Row 1: ESC, F1-F12
constexpr u16 ESCAPE = 1;
constexpr u16 F1 = 59;
constexpr u16 F2 = 60;
constexpr u16 F3 = 61;
constexpr u16 F4 = 62;
constexpr u16 F5 = 63;
constexpr u16 F6 = 64;
constexpr u16 F7 = 65;
constexpr u16 F8 = 66;
constexpr u16 F9 = 67;
constexpr u16 F10 = 68;
constexpr u16 F11 = 87;
constexpr u16 F12 = 88;

// Row 2: Number row
constexpr u16 GRAVE = 41; // `
constexpr u16 _1 = 2;
constexpr u16 _2 = 3;
constexpr u16 _3 = 4;
constexpr u16 _4 = 5;
constexpr u16 _5 = 6;
constexpr u16 _6 = 7;
constexpr u16 _7 = 8;
constexpr u16 _8 = 9;
constexpr u16 _9 = 10;
constexpr u16 _0 = 11;
constexpr u16 MINUS = 12;
constexpr u16 EQUAL = 13;
constexpr u16 BACKSPACE = 14;

// Row 3: QWERTY row
constexpr u16 TAB = 15;
constexpr u16 Q = 16;
constexpr u16 W = 17;
constexpr u16 E = 18;
constexpr u16 R = 19;
constexpr u16 T = 20;
constexpr u16 Y = 21;
constexpr u16 U = 22;
constexpr u16 I = 23;
constexpr u16 O = 24;
constexpr u16 P = 25;
constexpr u16 LEFT_BRACKET = 26;
constexpr u16 RIGHT_BRACKET = 27;
constexpr u16 BACKSLASH = 43;

// Row 4: Home row
constexpr u16 CAPS_LOCK = 58;
constexpr u16 A = 30;
constexpr u16 S = 31;
constexpr u16 D = 32;
constexpr u16 F = 33;
constexpr u16 G = 34;
constexpr u16 H = 35;
constexpr u16 J = 36;
constexpr u16 K = 37;
constexpr u16 L = 38;
constexpr u16 SEMICOLON = 39;
constexpr u16 APOSTROPHE = 40;
constexpr u16 ENTER = 28;

// Row 5: Bottom row
constexpr u16 LEFT_SHIFT = 42;
constexpr u16 Z = 44;
constexpr u16 X = 45;
constexpr u16 C = 46;
constexpr u16 V = 47;
constexpr u16 B = 48;
constexpr u16 N = 49;
constexpr u16 M = 50;
constexpr u16 COMMA = 51;
constexpr u16 DOT = 52;
constexpr u16 SLASH = 53;
constexpr u16 RIGHT_SHIFT = 54;

// Row 6: Bottom modifiers
constexpr u16 LEFT_CTRL = 29;
constexpr u16 LEFT_META = 125;
constexpr u16 LEFT_ALT = 56;
constexpr u16 SPACE = 57;
constexpr u16 RIGHT_ALT = 100;
constexpr u16 RIGHT_META = 126;
constexpr u16 RIGHT_CTRL = 97;

// Navigation cluster
constexpr u16 INSERT = 110;
constexpr u16 DELETE = 111;
constexpr u16 HOME = 102;
constexpr u16 END = 107;
constexpr u16 PAGE_UP = 104;
constexpr u16 PAGE_DOWN = 109;

// Arrow keys
constexpr u16 UP = 103;
constexpr u16 DOWN = 108;
constexpr u16 LEFT = 105;
constexpr u16 RIGHT = 106;
} // namespace key

// Note: modifier namespace is defined in input.hpp

/**
 * @brief Convert an evdev key code into an ASCII character.
 *
 * @details
 * Performs a best-effort translation based on the key code and the current
 * modifier state. Non-printable keys return `0`. Control combinations for
 * letters (Ctrl+A -> 0x01, etc.) are supported.
 *
 * @param code Evdev key code.
 * @param modifiers Current modifier bitmask (see @ref input::modifier).
 * @return ASCII character byte, or `0` if the key is non-printable.
 */
char key_to_ascii(u16 code, u8 modifiers);

/**
 * @brief Determine whether a key code represents a modifier key.
 *
 * @details
 * Modifier keys include Shift/Ctrl/Alt/Meta variants. Caps Lock is handled
 * separately because it toggles state rather than being momentary.
 *
 * @param code Evdev key code.
 * @return `true` if the code corresponds to a modifier key.
 */
bool is_modifier(u16 code);

/**
 * @brief Map a modifier key code to its modifier mask bit.
 *
 * @param code Evdev key code.
 * @return Modifier bit to set/clear (see @ref input::modifier), or 0 if not a
 *         recognized modifier key.
 */
u8 modifier_bit(u16 code);

} // namespace input
