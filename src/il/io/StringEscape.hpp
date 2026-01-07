//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares utility functions for encoding and decoding C-style escape
// sequences in string literals. These functions enable the parser and serializer
// to handle special characters (newlines, tabs, quotes, etc.) in IL string constants
// while maintaining ASCII-safe textual representations.
//
// String escaping is essential for IL text format because:
// - String literals may contain control characters that aren't printable
// - Quotes and backslashes need escaping to avoid syntax ambiguity
// - Non-ASCII bytes must be represented in a portable, inspectable way
// - Round-trip invariant: parse(serialize(str)) == str for all strings
//
// Supported Escape Sequences:
// - Standard escapes: \n (newline), \t (tab), \r (carriage return), \0 (null)
// - Character escapes: \\ (backslash), \" (double quote)
// - Hex escapes: \xNN for arbitrary bytes (e.g., \x1B for ESC)
//
// Encoding Strategy:
// The encoder converts control characters (0x00-0x1F, 0x7F) and special characters
// (backslash, quotes) into escape sequences. Printable ASCII and UTF-8 continuation
// bytes pass through unchanged, making the output human-readable when possible
// while ensuring it's always ASCII-safe.
//
// Decoding Strategy:
// The decoder recognizes standard escape sequences and hex escapes, validating
// that hex digits are well-formed. Unknown escape sequences are rejected with
// descriptive error messages.
//
// Design Decisions:
// - Stateless functions: No persistent state between encode/decode calls
// - UTF-8 aware: Preserves multi-byte UTF-8 sequences without escaping
// - Error reporting: Decode returns bool + optional error message string
// - Inline encoding: Defined in header for optimization (small hot function)
//
// Usage in IL Pipeline:
// - Parser: Calls decodeEscapedString on string literals from IL text
// - Serializer: Calls encodeEscapedString when writing string constants
// - Golden tests: Ensures canonical string representation for diffing
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdio>
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
inline std::string encodeEscapedString(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input)
    {
        switch (c)
        {
            case '\\':
                out.append("\\\\");
                break;
            case '\"':
                out.append("\\\"");
                break;
            case '\n':
                out.append("\\n");
                break;
            case '\r':
                out.append("\\r");
                break;
            case '\t':
                out.append("\\t");
                break;
            case '\0':
                out.append("\\0");
                break;
            default:
                if (c < 0x20 || c == 0x7F || c >= 0x80)
                {
                    char buf[5];
                    std::snprintf(buf, sizeof(buf), "\\x%02X", c);
                    out.append(buf);
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

} // namespace il::io
