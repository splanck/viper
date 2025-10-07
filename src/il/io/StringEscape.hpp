// File: src/il/io/StringEscape.hpp
// Purpose: Declare helpers for encoding and decoding escaped string literals.
// Key invariants: Decoders reject malformed escape sequences; encoders always
//                 produce ASCII-safe representations.
// Ownership/Lifetime: Stateless utility functions.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference
#pragma once

#include <string>
#include <string_view>

namespace il::io
{

/// @brief Decode common C-style escape sequences from @p input.
/// @param input String containing escape sequences like `\n`, `\t`, `\\`, `\"`, or `\xNN`.
/// @param output Destination for the decoded UTF-8 string.
/// @param error Optional pointer receiving a human-readable error message on failure.
/// @return True on success; false if @p input contains a malformed escape sequence.
bool decodeEscapedString(std::string_view input, std::string &output, std::string *error = nullptr);

/// @brief Encode control characters in @p input using C-style escape sequences.
/// @param input Raw UTF-8 string to encode.
/// @return Escaped representation safe for inclusion in IL text.
std::string encodeEscapedString(std::string_view input);

} // namespace il::io

