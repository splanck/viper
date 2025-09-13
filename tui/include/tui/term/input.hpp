// tui/include/tui/term/input.hpp
// @brief Decode UTF-8 byte streams into key events.
// @invariant Maintains UTF-8 state across feeds.
// @ownership Does not own input buffers; owns internal event queue.
#pragma once

#include <cstdint>
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
        Unknown
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

    uint32_t cp_{0};
    unsigned expected_{0};
    std::vector<KeyEvent> events_{};
};

} // namespace viper::tui::term
