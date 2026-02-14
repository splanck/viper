//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/StringUtils.hpp
// Purpose: Shared string utility functions for all language frontends.
// Key invariants: All functions are stateless and non-owning.
// Ownership/Lifetime: Header-only, no dynamic allocation.
// Links: frontends/common/CharUtils.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace il::frontends::common::string_utils
{

/// @brief Case-insensitive comparison of two string views.
/// @details Performs character-by-character comparison ignoring case using
///          std::toupper with unsigned char casts to avoid undefined behaviour
///          on negative char values.
/// @param a First string to compare.
/// @param b Second string to compare.
/// @return True if strings are equal ignoring case, false otherwise.
[[nodiscard]] inline bool iequals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;

    return std::equal(a.begin(),
                      a.end(),
                      b.begin(),
                      b.end(),
                      [](char ca, char cb)
                      {
                          return std::toupper(static_cast<unsigned char>(ca)) ==
                                 std::toupper(static_cast<unsigned char>(cb));
                      });
}

} // namespace il::frontends::common::string_utils
