//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/StringEscape.cpp
// Purpose: Implement string escape decoding helpers shared by IL text parsers.
// Links: docs/il-guide.md#literals
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines the helpers used to decode escape sequences in textual IL.
/// @details Separating the decoding logic keeps parser headers minimal and
///          centralises error handling so all clients present consistent
///          messages for malformed escape sequences.

#include "il/io/StringEscape.hpp"

#include <cctype>
#include <cstdio>

namespace il::io
{
namespace
{
/// @brief Convert a hexadecimal digit into its numeric value.
/// @details Accepts ASCII digits and lowercase or uppercase alphabetic digits.
///          Any other character yields zero; callers should validate input
///          before relying on the result because the helper performs no error
///          reporting itself.
/// @param c Character representing a single hexadecimal digit.
/// @return Numeric value in the range [0, 15]; zero when @p c is not a hex
///         digit.
unsigned hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<unsigned>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F')
        return static_cast<unsigned>(10 + (c - 'A'));
    return 0u;
}

/// @brief Determine whether a character is a valid hexadecimal digit.
/// @details Delegates to `std::isxdigit` while preserving the unsigned-char cast
///          used to avoid undefined behaviour on negative char values.
/// @param c Character to inspect.
/// @return True when @p c is a hexadecimal digit; false otherwise.
bool isHex(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace

/// @brief Decode escape sequences embedded in IL string literals.
/// @details Walks @p input left-to-right, copying ordinary characters directly
///          into @p output and interpreting escapes according to the IL
///          specification.  Supports C-style single-character escapes and
///          `\xNN` hexadecimal escapes.  Invalid sequences trigger early return
///          with an optional explanatory message for diagnostics.
/// @param input String view containing the escaped literal (without quotes).
/// @param output Destination string populated with the decoded characters.
/// @param error Optional pointer that receives an explanatory message when
///              decoding fails.
/// @return True when decoding succeeds; false and an error message otherwise.
bool decodeEscapedString(std::string_view input, std::string &output, std::string *error)
{
    output.clear();
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        char c = input[i];
        if (c != '\\')
        {
            output.push_back(c);
            continue;
        }
        if (i + 1 >= input.size())
        {
            if (error)
                *error = "unterminated escape sequence";
            return false;
        }
        char next = input[++i];
        switch (next)
        {
            case '\\':
                output.push_back('\\');
                break;
            case '\"':
                output.push_back('\"');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case '0':
                output.push_back('\0');
                break;
            case 'x':
            {
                if (i + 2 >= input.size() || !isHex(input[i + 1]) || !isHex(input[i + 2]))
                {
                    if (error)
                        *error = "invalid hex escape";
                    return false;
                }
                unsigned hi = hexValue(input[i + 1]);
                unsigned lo = hexValue(input[i + 2]);
                i += 2;
                char value = static_cast<char>((hi << 4) | lo);
                output.push_back(value);
                break;
            }
            default:
                if (error)
                {
                    char buf[64];
                    if (std::snprintf(buf, sizeof(buf), "unknown escape sequence \\%c", next) < 0)
                        *error = "unknown escape sequence";
                    else
                        *error = buf;
                }
                return false;
        }
    }
    return true;
}

} // namespace il::io
