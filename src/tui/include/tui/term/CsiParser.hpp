// tui/include/tui/term/CsiParser.hpp
// @brief Helpers for parsing Control Sequence Introducer (CSI) input.
// @invariant Produces key and mouse events for recognized sequences.
// @ownership Holds references to caller-owned event buffers.
#pragma once

#include "tui/term/key_event.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace viper::tui::term
{
/// @brief Result of handling a CSI sequence.
struct CsiResult
{
    bool start_paste{false}; ///< True when bracketed paste mode begins.
    bool handled{false};     ///< True if the sequence mapped to a known action.
};

/// @brief Parser for terminal CSI escape sequences.
/// @invariant Reuses caller-provided buffers for decoded events.
/// @ownership Holds references to event buffers owned by the caller.
class CsiParser
{
  public:
    /// @brief Construct a parser bound to output event buffers.
    CsiParser(std::vector<KeyEvent> &keys,
              std::vector<MouseEvent> &mouse,
              std::string &paste_buffer);

    /// @brief Handle a CSI sequence with final byte and parameter payload.
    /// @param final Terminal final byte.
    /// @param params Parameter bytes preceding the final.
    /// @return Parsed result indicating produced events and paste state.
    [[nodiscard]] CsiResult handle(char final, std::string_view params);

    /// @brief Parse a semicolon separated parameter list.
    [[nodiscard]] std::vector<int> parse_params(std::string_view params) const;

    /// @brief Decode modifier masks from VT-style numeric representation.
    [[nodiscard]] unsigned decode_mod(int value) const;

  private:
    void handle_sgr_mouse(char final, std::string_view params);

    std::vector<KeyEvent> &key_events_;
    std::vector<MouseEvent> &mouse_events_;
    std::string &paste_buffer_;
};

} // namespace viper::tui::term
