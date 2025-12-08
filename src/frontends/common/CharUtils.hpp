//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/CharUtils.hpp
// Purpose: Common character classification utilities for lexers.
//
// This header provides inline character classification functions that are
// useful for building lexers in various language frontends.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cctype>
#include <string>

namespace il::frontends::common::char_utils
{

/// @brief Check if character is an ASCII letter (A-Z, a-z).
[[nodiscard]] constexpr bool isLetter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// @brief Check if character is a decimal digit (0-9).
[[nodiscard]] constexpr bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

/// @brief Check if character is a hex digit (0-9, A-F, a-f).
[[nodiscard]] constexpr bool isHexDigit(char c) noexcept
{
    return isDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/// @brief Check if character is an octal digit (0-7).
[[nodiscard]] constexpr bool isOctalDigit(char c) noexcept
{
    return c >= '0' && c <= '7';
}

/// @brief Check if character is a binary digit (0-1).
[[nodiscard]] constexpr bool isBinaryDigit(char c) noexcept
{
    return c == '0' || c == '1';
}

/// @brief Check if character is alphanumeric (letter or digit).
[[nodiscard]] constexpr bool isAlphanumeric(char c) noexcept
{
    return isLetter(c) || isDigit(c);
}

/// @brief Check if character can start an identifier (letter or underscore).
/// @details Most languages allow identifiers to start with a letter or underscore.
[[nodiscard]] constexpr bool isIdentifierStart(char c) noexcept
{
    return isLetter(c) || c == '_';
}

/// @brief Check if character can continue an identifier (letter, digit, or underscore).
[[nodiscard]] constexpr bool isIdentifierContinue(char c) noexcept
{
    return isLetter(c) || isDigit(c) || c == '_';
}

/// @brief Check if character is ASCII whitespace.
[[nodiscard]] constexpr bool isWhitespace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// @brief Check if character is horizontal whitespace (space or tab).
[[nodiscard]] constexpr bool isHorizontalWhitespace(char c) noexcept
{
    return c == ' ' || c == '\t';
}

/// @brief Check if character is a newline (CR or LF).
[[nodiscard]] constexpr bool isNewline(char c) noexcept
{
    return c == '\r' || c == '\n';
}

/// @brief Convert ASCII character to lowercase.
[[nodiscard]] constexpr char toLower(char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c + ('a' - 'A'));
    return c;
}

/// @brief Convert ASCII character to uppercase.
[[nodiscard]] constexpr char toUpper(char c) noexcept
{
    if (c >= 'a' && c <= 'z')
        return static_cast<char>(c - ('a' - 'A'));
    return c;
}

/// @brief Convert string to lowercase (ASCII only).
[[nodiscard]] inline std::string toLowercase(const std::string &s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s)
    {
        result.push_back(toLower(c));
    }
    return result;
}

/// @brief Convert string to uppercase (ASCII only).
[[nodiscard]] inline std::string toUppercase(const std::string &s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s)
    {
        result.push_back(toUpper(c));
    }
    return result;
}

/// @brief Get the numeric value of a hex digit (0-15).
/// @return Value 0-15, or -1 if not a hex digit.
[[nodiscard]] constexpr int hexDigitValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/// @brief Get the numeric value of a digit (0-9).
/// @return Value 0-9, or -1 if not a digit.
[[nodiscard]] constexpr int digitValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    return -1;
}

} // namespace il::frontends::common::char_utils
