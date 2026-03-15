//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/EscapeSequences.hpp
// Purpose: Common escape sequence processing utilities for lexers.
// Key invariants:
//   - processEscape maps single-char escapes; returns nullopt for unknowns
//   - processHexEscape decodes 2 hex digit characters into a byte
//   - processUnicodeEscape decodes 4 hex digits and encodes to UTF-8
//   - codepointToUtf8 converts a Unicode codepoint to its UTF-8 encoding
// Ownership/Lifetime:
//   - Header-only, stateless utility functions
// Links: frontends/common/CharUtils.hpp, frontends/zia/Lexer.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/common/CharUtils.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace il::frontends::common::escape_sequences
{

/// @brief Map a simple escape character to its value.
/// @param c The character after the backslash (e.g., 'n' for \n).
/// @return The escaped character value, or nullopt for unrecognized escapes.
[[nodiscard]] inline std::optional<char> processEscape(char c) noexcept
{
    switch (c)
    {
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'b':
            return '\b';
        case 'a':
            return '\a';
        case 'f':
            return '\f';
        case 'v':
            return '\v';
        case '\\':
            return '\\';
        case '"':
            return '"';
        case '\'':
            return '\'';
        case '0':
            return '\0';
        case '$':
            return '$'; // For string interpolation escape
        default:
            return std::nullopt;
    }
}

/// @brief Process a hex escape sequence from two characters.
/// @param high The first hex digit character.
/// @param low The second hex digit character.
/// @return The decoded byte, or nullopt if either character is not a valid hex digit.
[[nodiscard]] inline std::optional<char> processHexEscape(char high, char low) noexcept
{
    int h = char_utils::hexDigitValue(high);
    if (h < 0)
        return std::nullopt;
    int l = char_utils::hexDigitValue(low);
    if (l < 0)
        return std::nullopt;
    return static_cast<char>((h << 4) | l);
}

/// @brief Convert a Unicode codepoint to its UTF-8 encoding.
/// @param codepoint Unicode codepoint (U+0000 to U+10FFFF).
/// @return The UTF-8 encoded string, or nullopt if the codepoint is out of range.
[[nodiscard]] inline std::optional<std::string> codepointToUtf8(uint32_t codepoint)
{
    std::string result;
    if (codepoint <= 0x7F)
    {
        result.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0x10FFFF)
    {
        result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        return std::nullopt; // Invalid codepoint
    }
    return result;
}

/// @brief Parse 4 hex digit characters into a Unicode codepoint.
/// @param digits Pointer to exactly 4 characters to parse.
/// @return The codepoint value, or nullopt if any character is not a valid hex digit.
[[nodiscard]] inline std::optional<uint32_t> parseUnicodeHexDigits(const char *digits) noexcept
{
    uint32_t codepoint = 0;
    for (int i = 0; i < 4; ++i)
    {
        int val = char_utils::hexDigitValue(digits[i]);
        if (val < 0)
            return std::nullopt;
        codepoint = (codepoint << 4) | static_cast<uint32_t>(val);
    }
    return codepoint;
}

/// @brief Process a unicode escape sequence from 4 hex digit characters.
/// @param digits Pointer to exactly 4 characters containing the hex digits.
/// @return The UTF-8 encoded string, or nullopt if the digits are invalid.
///
/// @details Parses the 4 hex digit characters and converts the resulting
///          codepoint to UTF-8. Supports the full BMP range (U+0000 to U+FFFF).
[[nodiscard]] inline std::optional<std::string> processUnicodeEscape(const char *digits)
{
    auto cp = parseUnicodeHexDigits(digits);
    if (!cp)
        return std::nullopt;
    return codepointToUtf8(*cp);
}

} // namespace il::frontends::common::escape_sequences
