//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/util/string.hpp
// Purpose: Provides string manipulation utilities for the TUI library.
// Key invariants: ASCII-only case conversion (no Unicode support).
// Ownership/Lifetime: Stateless inline utilities with no dynamic resources.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace viper::tui::util
{

/// @brief Convert a string to lowercase in-place.
/// @details Uses ASCII-only lowercase conversion via std::tolower.
/// @param s String to convert.
inline void toLowerInPlace(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
}

/// @brief Convert a string to lowercase, returning a new string.
/// @details Uses ASCII-only lowercase conversion via std::tolower.
/// @param s Input string.
/// @return Lowercase copy of the input string.
[[nodiscard]] inline std::string toLower(const std::string &s)
{
    std::string result = s;
    toLowerInPlace(result);
    return result;
}

} // namespace viper::tui::util
