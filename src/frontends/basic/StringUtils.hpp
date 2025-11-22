//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/StringUtils.hpp
// Purpose: String utility functions for BASIC frontend 
// Key invariants: All functions are constexpr-compatible where possible
// Ownership/Lifetime: All functions are stateless and non-owning
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace il::frontends::basic::string_utils
{

/// @brief Case-insensitive comparison of two strings.
/// @details Performs character-by-character comparison ignoring case.
///          This is more efficient than converting both strings to uppercase
///          and then comparing, as it avoids allocations.
/// @param a First string to compare.
/// @param b Second string to compare.
/// @return True if strings are equal ignoring case, false otherwise.
/// @example
/// ```cpp
/// if (iequals(tok.lexeme, "INTEGER")) { /* ... */ }
/// ```
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

/// @brief Check if a string starts with a prefix (case-insensitive).
/// @param str String to check.
/// @param prefix Prefix to look for.
/// @return True if str starts with prefix, ignoring case.
[[nodiscard]] inline bool istarts_with(std::string_view str, std::string_view prefix) noexcept
{
    if (str.size() < prefix.size())
        return false;

    return iequals(str.substr(0, prefix.size()), prefix);
}

/// @brief Check if a string ends with a suffix (case-insensitive).
/// @param str String to check.
/// @param suffix Suffix to look for.
/// @return True if str ends with suffix, ignoring case.
[[nodiscard]] inline bool iends_with(std::string_view str, std::string_view suffix) noexcept
{
    if (str.size() < suffix.size())
        return false;

    return iequals(str.substr(str.size() - suffix.size()), suffix);
}

/// @brief Convert a string to uppercase (allocating version).
/// @param str String to convert.
/// @return New string with all characters converted to uppercase.
/// @note Use sparingly; prefer iequals() for comparisons to avoid allocation.
[[nodiscard]] inline std::string to_upper(std::string_view str)
{
    std::string result;
    result.reserve(str.size());
    for (char c : str)
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return result;
}

/// @brief Convert a string to lowercase (allocating version).
/// @param str String to convert.
/// @return New string with all characters converted to lowercase.
[[nodiscard]] inline std::string to_lower(std::string_view str)
{
    std::string result;
    result.reserve(str.size());
    for (char c : str)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return result;
}

/// @brief Trim leading and trailing whitespace.
/// @param str String to trim.
/// @return String view with leading/trailing whitespace removed.
[[nodiscard]] inline std::string_view trim(std::string_view str) noexcept
{
    auto start = str.begin();
    auto end = str.end();

    while (start != end && std::isspace(static_cast<unsigned char>(*start)))
        ++start;

    while (start != end && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;

    return std::string_view(start, std::distance(start, end));
}

} // namespace il::frontends::basic::string_utils
