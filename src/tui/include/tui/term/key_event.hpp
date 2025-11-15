// tui/include/tui/term/key_event.hpp
// @brief POD event types shared across terminal input and UI layers.
// @invariant Enumeration values match terminal decoding logic.
// @ownership Simple value types with no dynamic ownership.
#pragma once

#include <cstdint>
#include <string>

namespace viper::tui::term
{
struct KeyEvent
{
    enum class Code
    {
        Enter,
        Esc,
        Tab,
        Backspace,
        Up,
        Down,
        Left,
        Right,
        Home,
        End,
        PageUp,
        PageDown,
        Insert,
        Delete,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        Unknown
    };

    enum Mods : unsigned
    {
        Shift = 1,
        Alt = 2,
        Ctrl = 4
    };

    uint32_t codepoint{0};
    Code code{Code::Unknown};
    unsigned mods{0};
};

struct MouseEvent
{
    enum class Type
    {
        Down,
        Up,
        Move,
        Wheel
    };

    Type type{Type::Move};
    int x{0};
    int y{0};
    unsigned buttons{0};
    unsigned mods{0};
};

struct PasteEvent
{
    std::string text{};
};

} // namespace viper::tui::term
