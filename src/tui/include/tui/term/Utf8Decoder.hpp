//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/term/Utf8Decoder.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace viper::tui::term
{
/// @brief Result of decoding a single byte of UTF-8 data.
struct Utf8Result
{
    bool has_codepoint{false}; ///< True when a complete codepoint was produced.
    uint32_t codepoint{0};     ///< The decoded Unicode scalar value when available.
    bool error{false};         ///< Set when an invalid sequence was encountered.
    bool replay{false};        ///< Request the caller to replay the current byte.
};

/// @brief Stateful UTF-8 decoder that accepts bytes incrementally.
/// @invariant Keeps track of remaining continuation bytes for current sequence.
/// @ownership Does not own external buffers; only internal state.
class Utf8Decoder
{
  public:
    /// @brief Decode the next byte of UTF-8 data.
    /// @param byte Next byte from the input stream.
    /// @return Information about produced codepoints and error handling.
    [[nodiscard]] Utf8Result feed(unsigned char byte) noexcept;

    /// @brief Whether the decoder is currently idle (no pending continuation).
    [[nodiscard]] bool idle() const noexcept;

    /// @brief Reset decoder to initial state discarding partial sequence.
    void reset() noexcept;

  private:
    uint32_t cp_{0};
    unsigned expected_{0};
    unsigned length_{0};
};

} // namespace viper::tui::term
