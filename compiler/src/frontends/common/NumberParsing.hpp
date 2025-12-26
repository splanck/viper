//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/NumberParsing.hpp
// Purpose: Common number parsing utilities for language frontends.
//
// This header provides utilities for parsing numeric literals that are shared
// across multiple language frontends (BASIC, Pascal, etc.).
//
//===----------------------------------------------------------------------===//
#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace il::frontends::common::number_parsing
{

/// @brief Result of parsing a numeric literal.
struct ParsedNumber
{
    bool isFloat = false;    ///< True if number has decimal point or exponent
    int64_t intValue = 0;    ///< Integer value (valid when !isFloat)
    double floatValue = 0.0; ///< Float value (valid when isFloat)
    bool overflow = false;   ///< True if value overflowed during parsing
    bool valid = true;       ///< True if parsing succeeded
};

/// @brief Parse a decimal numeric literal from text.
/// @param text The numeric literal text (digits, optional decimal, optional exponent)
/// @return ParsedNumber with the parsed value
/// @details Handles formats like: 123, 123.45, 1.23e10, 1E-5
[[nodiscard]] inline ParsedNumber parseDecimalLiteral(std::string_view text)
{
    ParsedNumber result;

    if (text.empty())
    {
        result.valid = false;
        return result;
    }

    // Check if it's a float (has decimal point or exponent)
    bool hasDecimal = text.find('.') != std::string_view::npos;
    bool hasExponent =
        text.find('e') != std::string_view::npos || text.find('E') != std::string_view::npos;

    result.isFloat = hasDecimal || hasExponent;

    if (result.isFloat)
    {
        // Parse as double using strtod for portability
        std::string textStr(text);
        char *endPtr = nullptr;
        result.floatValue = std::strtod(textStr.c_str(), &endPtr);

        if (endPtr == textStr.c_str())
        {
            result.valid = false;
        }
    }
    else
    {
        // Parse as integer using from_chars
        auto parseResult = std::from_chars(text.data(), text.data() + text.size(), result.intValue);

        if (parseResult.ec == std::errc::result_out_of_range)
        {
            result.overflow = true;
            result.valid = false;
        }
        else if (parseResult.ec != std::errc{})
        {
            result.valid = false;
        }
    }

    return result;
}

/// @brief Parse a hexadecimal integer literal from text.
/// @param text The hex literal text (hex digits only, no prefix)
/// @return ParsedNumber with the parsed value (always integer)
/// @details Expects text without prefix (e.g., "DEADBEEF" not "$DEADBEEF" or "0xDEADBEEF")
[[nodiscard]] inline ParsedNumber parseHexLiteral(std::string_view text)
{
    ParsedNumber result;
    result.isFloat = false;

    if (text.empty())
    {
        result.valid = false;
        return result;
    }

    auto parseResult = std::from_chars(text.data(), text.data() + text.size(), result.intValue, 16);

    if (parseResult.ec == std::errc::result_out_of_range)
    {
        result.overflow = true;
        result.valid = false;
    }
    else if (parseResult.ec != std::errc{})
    {
        result.valid = false;
    }

    return result;
}

/// @brief Parse a binary integer literal from text.
/// @param text The binary literal text (0s and 1s only, no prefix)
/// @return ParsedNumber with the parsed value (always integer)
[[nodiscard]] inline ParsedNumber parseBinaryLiteral(std::string_view text)
{
    ParsedNumber result;
    result.isFloat = false;

    if (text.empty())
    {
        result.valid = false;
        return result;
    }

    auto parseResult = std::from_chars(text.data(), text.data() + text.size(), result.intValue, 2);

    if (parseResult.ec == std::errc::result_out_of_range)
    {
        result.overflow = true;
        result.valid = false;
    }
    else if (parseResult.ec != std::errc{})
    {
        result.valid = false;
    }

    return result;
}

/// @brief Parse an octal integer literal from text.
/// @param text The octal literal text (0-7 digits only, no prefix)
/// @return ParsedNumber with the parsed value (always integer)
[[nodiscard]] inline ParsedNumber parseOctalLiteral(std::string_view text)
{
    ParsedNumber result;
    result.isFloat = false;

    if (text.empty())
    {
        result.valid = false;
        return result;
    }

    auto parseResult = std::from_chars(text.data(), text.data() + text.size(), result.intValue, 8);

    if (parseResult.ec == std::errc::result_out_of_range)
    {
        result.overflow = true;
        result.valid = false;
    }
    else if (parseResult.ec != std::errc{})
    {
        result.valid = false;
    }

    return result;
}

/// @brief Check if a character could start a numeric literal.
/// @param c The character to check
/// @return True if c is a digit
[[nodiscard]] constexpr bool isNumberStart(char c) noexcept
{
    return c >= '0' && c <= '9';
}

/// @brief Check if a character is a valid exponent indicator.
/// @param c The character to check
/// @return True if c is 'e' or 'E'
[[nodiscard]] constexpr bool isExponentChar(char c) noexcept
{
    return c == 'e' || c == 'E';
}

/// @brief Check if a character is a sign for exponent.
/// @param c The character to check
/// @return True if c is '+' or '-'
[[nodiscard]] constexpr bool isSignChar(char c) noexcept
{
    return c == '+' || c == '-';
}

} // namespace il::frontends::common::number_parsing
