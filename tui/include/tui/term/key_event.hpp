// tui/include/tui/term/key_event.hpp
// @brief Lightweight POD event types shared between term input and UI layers.
// @invariant Modifier bits use the Mods enum; Code enumerators cover supported keys.
// @ownership Plain value types; no ownership semantics.
#pragma once

#include <cstdint>
#include <string>

namespace viper::tui::term
{
/// @brief Discrete keyboard key event with optional Unicode codepoint.
struct KeyEvent
{
    /// @brief Logical key codes recognised by the terminal decoder.
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

    /// @brief Modifier bit flags associated with a key press.
    enum Mods : unsigned
    {
        Shift = 1,
        Alt = 2,
        Ctrl = 4
    };

    uint32_t codepoint{0}; ///< Unicode codepoint when available.
    Code code{Code::Unknown}; ///< Logical key identifier.
    unsigned mods{0};        ///< Modifier mask composed of Mods values.
};

/// @brief Terminal mouse event with button/modifier information.
struct MouseEvent
{
    enum class Type
    {
        Down,
        Up,
        Move,
        Wheel
    };

    Type type{Type::Move}; ///< Event kind.
    int x{0};              ///< Column coordinate.
    int y{0};              ///< Row coordinate.
    unsigned buttons{0};   ///< Button bitmask.
    unsigned mods{0};      ///< Modifier mask.
};

/// @brief Paste event carrying textual data.
struct PasteEvent
{
    std::string text{};
};

} // namespace viper::tui::term
