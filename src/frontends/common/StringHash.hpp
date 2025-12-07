//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/StringHash.hpp
// Purpose: Heterogeneous string hash functor for C++20 unordered containers.
//
// This enables lookup with std::string_view keys in unordered_map/set
// without allocating temporary std::string objects.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace il::frontends::common
{

/// @brief Hash functor for heterogeneous string lookup (C++20).
/// @details Enables lookup with std::string_view keys in unordered_map
///          without allocating temporary std::string objects.
struct StringHash
{
    using is_transparent = void;

    template <typename T>
    [[nodiscard]] std::size_t operator()(const T &key) const noexcept
    {
        return std::hash<std::string_view>{}(std::string_view(key));
    }
};

/// @brief Utility to convert a string to lowercase.
/// @param s Input string.
/// @return Lowercase copy of the string.
inline std::string toLower(const std::string &s)
{
    std::string result = s;
    for (char &c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

/// @brief Case-insensitive string comparison.
/// @param a First string.
/// @param b Second string.
/// @return True if strings are equal ignoring case.
inline bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

/// @brief Case-insensitive string hash functor.
/// @details Enables case-insensitive lookup in unordered containers.
struct CaseInsensitiveHash
{
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view key) const noexcept
    {
        std::size_t hash = 0;
        for (char c : key)
        {
            hash = hash * 31 + static_cast<std::size_t>(std::tolower(static_cast<unsigned char>(c)));
        }
        return hash;
    }

    [[nodiscard]] std::size_t operator()(const std::string &key) const noexcept
    {
        return (*this)(std::string_view(key));
    }
};

/// @brief Case-insensitive string equality functor.
struct CaseInsensitiveEqual
{
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept
    {
        return equalsIgnoreCase(a, b);
    }
};

} // namespace il::frontends::common
