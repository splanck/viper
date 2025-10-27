//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements helpers that parse `--break` specifications for the ilc command
// line. These helpers accept either label names or `file:line` breakpoints and
// perform the minimal validation used by the debugger entry point.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parses breakpoint specifications accepted by the ilc debugger mode.
/// @details Only a single helper lives here today, but centralising it keeps the
///          parsing rules consistent between command-line entry points.

#include "break_spec.hpp"

#include <cctype>

namespace ilc
{

/// @brief Determine whether a breakpoint specification targets a source file.
///
/// @details The parser accepts breakpoints of the form `path:line`. Validation
///          proceeds as follows:
///          1. Ensure a colon delimiter exists and characters follow it.
///          2. Confirm every character after the colon is a digit, yielding a
///             positive line number.
///          3. Require the prefix to contain at least one non-whitespace
///             character so empty or padded strings are rejected.
///
/// @param spec Command-line argument to analyse.
/// @return True when @p spec names a source breakpoint; false otherwise.
bool isSrcBreakSpec(const std::string &spec)
{
    // Position of the colon separating file and line.
    auto pos = spec.rfind(':');
    if (pos == std::string::npos || pos + 1 >= spec.size())
        return false;

    // Skip whitespace between the colon and the first digit.
    auto linePos = pos + 1;
    while (linePos < spec.size() &&
           std::isspace(static_cast<unsigned char>(spec[linePos])))
    {
        ++linePos;
    }
    if (linePos >= spec.size())
        return false;

    // Ensure characters forming the line component are digits, permitting
    // trailing whitespace after them.
    bool sawDigit = false;
    std::string::size_type i = linePos;
    for (; i < spec.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(spec[i]);
        if (std::isdigit(ch))
        {
            sawDigit = true;
            continue;
        }
        if (std::isspace(ch))
            break;
        return false;
    }
    if (!sawDigit)
        return false;
    for (; i < spec.size(); ++i)
    {
        if (!std::isspace(static_cast<unsigned char>(spec[i])))
            return false;
    }

    // Portion before the colon must contain non-whitespace characters.
    for (std::string::size_type i = 0; i < pos; ++i)
    {
        if (!std::isspace(static_cast<unsigned char>(spec[i])))
            return true;
    }

    return false;
}

} // namespace ilc
