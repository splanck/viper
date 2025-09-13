// tui/include/tui/term/input.hpp
// @brief Decode UTF-8 byte streams into key events.
// @invariant Maintains UTF-8 state across feeds.
// @ownership Does not own input buffers; owns internal event queue.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

/// @brief Incremental UTF-8 decoder producing key events.
/// @invariant Handles partial sequences across feed() calls.
/// @ownership Stores decoded events internally.
class InputDecoder
{
  public:
    /// @brief Feed bytes into decoder.
    /// @param bytes UTF-8 encoded data.
    void feed(std::string_view bytes);

    /// @brief Retrieve decoded events.
    /// @return Collected key events; internal queue is cleared.
    [[nodiscard]] std::vector<KeyEvent> drain();

  private:
    void emit(uint32_t cp);
    void handle_csi(char final, std::string_view params);
    void handle_ss3(char final, std::string_view params);
    static unsigned decode_mod(int value);

    enum class State
    {
        Utf8,
        Esc,
        CSI,
        SS3
    };

    State state_{State::Utf8};
    std::string seq_{};
    uint32_t cp_{0};
    unsigned expected_{0};
    std::vector<KeyEvent> events_{};
};

} // namespace viper::tui::term
