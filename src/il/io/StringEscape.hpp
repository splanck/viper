// File: src/il/io/StringEscape.hpp
// Purpose: Declare helpers for encoding and decoding escaped string literals.
// Key invariants: Decoders reject malformed escape sequences; encoders always
//                 produce ASCII-safe representations.
// Ownership/Lifetime: Stateless utility functions.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference
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
                if (c < 0x20 || c == 0x7F)
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
