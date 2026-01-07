//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/term/input.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/term/CsiParser.hpp"
#include "tui/term/Utf8Decoder.hpp"
#include "tui/term/key_event.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace viper::tui::term
{
/// @brief Incremental UTF-8 decoder producing terminal input events.
/// @invariant Handles partial sequences across feed() calls.
/// @ownership Stores decoded events internally.
class InputDecoder
{
  public:
    /// @brief Create a decoder with empty output queues.
    InputDecoder();

    /// @brief Feed bytes into decoder.
    /// @param bytes UTF-8 encoded data.
    void feed(std::string_view bytes);

    /// @brief Retrieve decoded events.
    /// @return Collected key events; internal queue is cleared.
    [[nodiscard]] std::vector<KeyEvent> drain();
    [[nodiscard]] std::vector<MouseEvent> drain_mouse();
    [[nodiscard]] std::vector<PasteEvent> drain_paste();

  private:
    enum class State
    {
        Utf8,
        Esc,
        CSI,
        SS3,
        Paste,
        PasteEsc,
        PasteCSI
    };

    void emit(uint32_t cp);
    State handle_csi(char final, std::string_view params);
    void handle_ss3(char final, std::string_view params);

    State state_{State::Utf8};
    std::string seq_{};
    Utf8Decoder utf8_decoder_{};
    std::vector<KeyEvent> key_events_{};
    std::vector<MouseEvent> mouse_events_{};
    std::vector<PasteEvent> paste_events_{};
    std::string paste_buf_{};
    CsiParser csi_parser_;
};

} // namespace viper::tui::term
