//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Utf8Decoder class, a stateful byte-at-a-time
// UTF-8 decoder for Viper's TUI input processing pipeline. The decoder
// accepts individual bytes via feed() and produces Unicode code points
// when complete multi-byte sequences are recognized.
//
// The decoder correctly handles all valid UTF-8 sequences (1-4 bytes)
// and detects invalid sequences, signaling errors via the Utf8Result
// struct. The replay flag allows the caller to re-process a byte that
// started a new sequence after an error was detected in the previous one.
//
// Key invariants:
//   - idle() returns true when no continuation bytes are pending.
//   - After an error, reset() returns the decoder to idle state.
//   - Valid code points range from U+0000 to U+10FFFF (excluding surrogates).
//
// Ownership: Utf8Decoder holds only scalar state (no heap allocation).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace viper::tui::term
{
/// @brief Result of decoding a single byte through the UTF-8 state machine.
/// @details Communicates whether a complete code point was produced, whether an
///          encoding error was detected, and whether the caller should replay the
///          current byte (used when a new sequence starts after an error mid-sequence).
struct Utf8Result
{
    bool has_codepoint{false}; ///< True when a complete codepoint was produced.
    uint32_t codepoint{0};     ///< The decoded Unicode scalar value when available.
    bool error{false};         ///< Set when an invalid sequence was encountered.
    bool replay{false};        ///< Request the caller to replay the current byte.
};

/// @brief Stateful UTF-8 decoder that processes bytes one at a time.
/// @details Implements the UTF-8 state machine for incremental byte-stream decoding.
///          Handles multi-byte sequences (2-4 bytes), detects overlong encodings
///          and invalid continuation bytes, and signals errors via the Utf8Result struct.
///          Designed for use in the terminal input pipeline where bytes arrive
///          one at a time from non-blocking reads.
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
