//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/util/color.cpp
// Purpose: Implements color parsing utilities for the TUI library.
// Key invariants: Parses hex RGB triplets into RGBA structs.
// Ownership/Lifetime: Stateless utility; no dynamic allocations.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements hex color parsing for the TUI rendering subsystem.
/// @details Converts "#RRGGBB" or "RRGGBB" strings to RGBA values.

#include "tui/util/color.hpp"

#include <cctype>

namespace viper::tui::util
{

bool parseHexColor(const std::string &s, render::RGBA &out)
{
    if (s.empty())
    {
        return false;
    }

    // Skip leading '#' if present
    std::size_t start = (s[0] == '#') ? 1 : 0;

    // Must have exactly 6 hex digits after optional #
    if (s.size() - start != 6)
    {
        return false;
    }

    // Validate all characters are hex digits
    for (std::size_t i = start; i < s.size(); ++i)
    {
        if (!std::isxdigit(static_cast<unsigned char>(s[i])))
        {
            return false;
        }
    }

    // Parse the hex value manually to avoid exceptions
    unsigned value = 0;
    for (std::size_t i = start; i < s.size(); ++i)
    {
        char c = s[i];
        unsigned digit = 0;
        if (c >= '0' && c <= '9')
        {
            digit = static_cast<unsigned>(c - '0');
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = static_cast<unsigned>(c - 'A' + 10);
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = static_cast<unsigned>(c - 'a' + 10);
        }
        value = (value << 4) | digit;
    }

    out.r = static_cast<uint8_t>((value >> 16) & 0xFF);
    out.g = static_cast<uint8_t>((value >> 8) & 0xFF);
    out.b = static_cast<uint8_t>(value & 0xFF);
    out.a = 255;
    return true;
}

} // namespace viper::tui::util
