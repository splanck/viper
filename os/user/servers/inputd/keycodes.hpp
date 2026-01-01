/**
 * @file keycodes.hpp
 * @brief Linux evdev key codes and translation helpers.
 *
 * @details
 * QEMU's virtio-keyboard device reports key events using Linux evdev key codes.
 * This header defines the subset of key codes used by the input server.
 */

#pragma once

#include <stdint.h>

namespace input
{

// Linux evdev keycodes (from linux/input-event-codes.h)
namespace key
{
constexpr uint16_t NONE = 0;

// Row 1: ESC, F1-F12
constexpr uint16_t ESCAPE = 1;
constexpr uint16_t F1 = 59;
constexpr uint16_t F2 = 60;
constexpr uint16_t F3 = 61;
constexpr uint16_t F4 = 62;
constexpr uint16_t F5 = 63;
constexpr uint16_t F6 = 64;
constexpr uint16_t F7 = 65;
constexpr uint16_t F8 = 66;
constexpr uint16_t F9 = 67;
constexpr uint16_t F10 = 68;
constexpr uint16_t F11 = 87;
constexpr uint16_t F12 = 88;

// Row 2: Number row
constexpr uint16_t GRAVE = 41; // `
constexpr uint16_t _1 = 2;
constexpr uint16_t _2 = 3;
constexpr uint16_t _3 = 4;
constexpr uint16_t _4 = 5;
constexpr uint16_t _5 = 6;
constexpr uint16_t _6 = 7;
constexpr uint16_t _7 = 8;
constexpr uint16_t _8 = 9;
constexpr uint16_t _9 = 10;
constexpr uint16_t _0 = 11;
constexpr uint16_t MINUS = 12;
constexpr uint16_t EQUAL = 13;
constexpr uint16_t BACKSPACE = 14;

// Row 3: QWERTY row
constexpr uint16_t TAB = 15;
constexpr uint16_t Q = 16;
constexpr uint16_t W = 17;
constexpr uint16_t E = 18;
constexpr uint16_t R = 19;
constexpr uint16_t T = 20;
constexpr uint16_t Y = 21;
constexpr uint16_t U = 22;
constexpr uint16_t I = 23;
constexpr uint16_t O = 24;
constexpr uint16_t P = 25;
constexpr uint16_t LEFT_BRACKET = 26;
constexpr uint16_t RIGHT_BRACKET = 27;
constexpr uint16_t BACKSLASH = 43;

// Row 4: Home row
constexpr uint16_t CAPS_LOCK = 58;
constexpr uint16_t A = 30;
constexpr uint16_t S = 31;
constexpr uint16_t D = 32;
constexpr uint16_t F = 33;
constexpr uint16_t G = 34;
constexpr uint16_t H = 35;
constexpr uint16_t J = 36;
constexpr uint16_t K = 37;
constexpr uint16_t L = 38;
constexpr uint16_t SEMICOLON = 39;
constexpr uint16_t APOSTROPHE = 40;
constexpr uint16_t ENTER = 28;

// Row 5: Bottom row
constexpr uint16_t LEFT_SHIFT = 42;
constexpr uint16_t Z = 44;
constexpr uint16_t X = 45;
constexpr uint16_t C = 46;
constexpr uint16_t V = 47;
constexpr uint16_t B = 48;
constexpr uint16_t N = 49;
constexpr uint16_t M = 50;
constexpr uint16_t COMMA = 51;
constexpr uint16_t DOT = 52;
constexpr uint16_t SLASH = 53;
constexpr uint16_t RIGHT_SHIFT = 54;

// Row 6: Bottom modifiers
constexpr uint16_t LEFT_CTRL = 29;
constexpr uint16_t LEFT_META = 125;
constexpr uint16_t LEFT_ALT = 56;
constexpr uint16_t SPACE = 57;
constexpr uint16_t RIGHT_ALT = 100;
constexpr uint16_t RIGHT_META = 126;
constexpr uint16_t RIGHT_CTRL = 97;

// Navigation cluster
constexpr uint16_t INSERT = 110;
constexpr uint16_t DELETE = 111;
constexpr uint16_t HOME = 102;
constexpr uint16_t END = 107;
constexpr uint16_t PAGE_UP = 104;
constexpr uint16_t PAGE_DOWN = 109;

// Arrow keys
constexpr uint16_t UP = 103;
constexpr uint16_t DOWN = 108;
constexpr uint16_t LEFT = 105;
constexpr uint16_t RIGHT = 106;
} // namespace key

// Modifier bits (same as in input_protocol.hpp)
namespace modifier
{
constexpr uint8_t SHIFT = 0x01;
constexpr uint8_t CTRL = 0x02;
constexpr uint8_t ALT = 0x04;
constexpr uint8_t META = 0x08;
constexpr uint8_t CAPS_LOCK = 0x10;
} // namespace modifier

/**
 * @brief Check if a key code is a modifier key.
 */
inline bool is_modifier(uint16_t code)
{
    return code == key::LEFT_SHIFT || code == key::RIGHT_SHIFT ||
           code == key::LEFT_CTRL || code == key::RIGHT_CTRL ||
           code == key::LEFT_ALT || code == key::RIGHT_ALT ||
           code == key::LEFT_META || code == key::RIGHT_META;
}

/**
 * @brief Get the modifier bit for a modifier key code.
 */
inline uint8_t modifier_bit(uint16_t code)
{
    switch (code)
    {
        case key::LEFT_SHIFT:
        case key::RIGHT_SHIFT:
            return modifier::SHIFT;
        case key::LEFT_CTRL:
        case key::RIGHT_CTRL:
            return modifier::CTRL;
        case key::LEFT_ALT:
        case key::RIGHT_ALT:
            return modifier::ALT;
        case key::LEFT_META:
        case key::RIGHT_META:
            return modifier::META;
        default:
            return 0;
    }
}

/**
 * @brief Translate an evdev keycode into an ASCII character.
 *
 * @param code Evdev key code.
 * @param modifiers Current modifier bitmask.
 * @return ASCII character byte, or 0 if non-printable.
 */
inline char key_to_ascii(uint16_t code, uint8_t modifiers)
{
    bool shift = (modifiers & modifier::SHIFT) != 0;
    bool caps = (modifiers & modifier::CAPS_LOCK) != 0;
    bool ctrl = (modifiers & modifier::CTRL) != 0;

    // Letters
    char letter = 0;
    if (code == key::A) letter = 'a';
    else if (code == key::B) letter = 'b';
    else if (code == key::C) letter = 'c';
    else if (code == key::D) letter = 'd';
    else if (code == key::E) letter = 'e';
    else if (code == key::F) letter = 'f';
    else if (code == key::G) letter = 'g';
    else if (code == key::H) letter = 'h';
    else if (code == key::I) letter = 'i';
    else if (code == key::J) letter = 'j';
    else if (code == key::K) letter = 'k';
    else if (code == key::L) letter = 'l';
    else if (code == key::M) letter = 'm';
    else if (code == key::N) letter = 'n';
    else if (code == key::O) letter = 'o';
    else if (code == key::P) letter = 'p';
    else if (code == key::Q) letter = 'q';
    else if (code == key::R) letter = 'r';
    else if (code == key::S) letter = 's';
    else if (code == key::T) letter = 't';
    else if (code == key::U) letter = 'u';
    else if (code == key::V) letter = 'v';
    else if (code == key::W) letter = 'w';
    else if (code == key::X) letter = 'x';
    else if (code == key::Y) letter = 'y';
    else if (code == key::Z) letter = 'z';

    if (letter != 0)
    {
        if (ctrl)
        {
            return static_cast<char>(letter - 'a' + 1);
        }
        bool uppercase = shift ^ caps;
        return uppercase ? (letter - 32) : letter;
    }

    // Numbers and symbols
    switch (code)
    {
        case key::_1: return shift ? '!' : '1';
        case key::_2: return shift ? '@' : '2';
        case key::_3: return shift ? '#' : '3';
        case key::_4: return shift ? '$' : '4';
        case key::_5: return shift ? '%' : '5';
        case key::_6: return shift ? '^' : '6';
        case key::_7: return shift ? '&' : '7';
        case key::_8: return shift ? '*' : '8';
        case key::_9: return shift ? '(' : '9';
        case key::_0: return shift ? ')' : '0';

        case key::MINUS: return shift ? '_' : '-';
        case key::EQUAL: return shift ? '+' : '=';
        case key::LEFT_BRACKET: return shift ? '{' : '[';
        case key::RIGHT_BRACKET: return shift ? '}' : ']';
        case key::BACKSLASH: return shift ? '|' : '\\';
        case key::SEMICOLON: return shift ? ':' : ';';
        case key::APOSTROPHE: return shift ? '"' : '\'';
        case key::GRAVE: return shift ? '~' : '`';
        case key::COMMA: return shift ? '<' : ',';
        case key::DOT: return shift ? '>' : '.';
        case key::SLASH: return shift ? '?' : '/';

        case key::SPACE: return ' ';
        case key::ENTER: return '\n';
        case key::TAB: return '\t';
        case key::BACKSPACE: return '\b';
        case key::ESCAPE: return '\033';

        default: return 0;
    }
}

} // namespace input
