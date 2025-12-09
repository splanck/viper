//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/KeywordTable.hpp
// Purpose: Common keyword lookup utilities for language frontends.
//
// This header provides template utilities for efficient keyword matching
// that are shared across language frontends.
//
// Key Features:
//   - Sorted array with binary search for compile-time keyword tables
//   - constexpr verification of table sorting
//   - Case-insensitive matching support
//
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace il::frontends::common::keyword_table
{

/// @brief A keyword entry mapping a lexeme to a token kind.
/// @tparam TokenKind The token kind enum type.
template <typename TokenKind>
struct KeywordEntry
{
    std::string_view lexeme; ///< The keyword text (usually uppercase).
    TokenKind kind;          ///< The token kind for this keyword.
};

/// @brief Check if a keyword table is properly sorted.
/// @details Used for static_assert validation of compile-time tables.
/// @tparam TokenKind The token kind enum type.
/// @tparam N Size of the table.
/// @param table The keyword table to check.
/// @return True if the table is lexicographically sorted.
template <typename TokenKind, std::size_t N>
[[nodiscard]] constexpr bool isKeywordTableSorted(const std::array<KeywordEntry<TokenKind>, N> &table)
{
    for (std::size_t i = 1; i < N; ++i)
    {
        if (!(table[i - 1].lexeme < table[i].lexeme))
            return false;
    }
    return true;
}

/// @brief Binary search lookup in a sorted keyword table.
/// @tparam TokenKind The token kind enum type.
/// @tparam N Size of the table.
/// @param table The sorted keyword table.
/// @param lexeme The lexeme to look up.
/// @return The token kind if found, std::nullopt otherwise.
template <typename TokenKind, std::size_t N>
[[nodiscard]] std::optional<TokenKind> lookupKeywordBinary(
    const std::array<KeywordEntry<TokenKind>, N> &table,
    std::string_view lexeme)
{
    auto first = table.begin();
    auto last = table.end();

    while (first < last)
    {
        auto mid = first + (last - first) / 2;
        if (mid->lexeme == lexeme)
            return mid->kind;
        if (mid->lexeme < lexeme)
            first = mid + 1;
        else
            last = mid;
    }

    return std::nullopt;
}

/// @brief Hash-based keyword table for runtime keyword lookup.
/// @tparam TokenKind The token kind enum type.
template <typename TokenKind>
class KeywordMap
{
  public:
    /// @brief Construct an empty keyword map.
    KeywordMap() = default;

    /// @brief Construct from a sorted array of keyword entries.
    /// @tparam N Size of the array.
    /// @param entries Array of keyword entries to populate the map.
    template <std::size_t N>
    explicit KeywordMap(const std::array<KeywordEntry<TokenKind>, N> &entries)
    {
        for (const auto &entry : entries)
            map_.emplace(std::string(entry.lexeme), entry.kind);
    }

    /// @brief Add a keyword to the map.
    /// @param lexeme The keyword text.
    /// @param kind The token kind.
    void add(std::string_view lexeme, TokenKind kind)
    {
        map_.emplace(std::string(lexeme), kind);
    }

    /// @brief Look up a keyword in the map.
    /// @param lexeme The lexeme to look up.
    /// @return The token kind if found, std::nullopt otherwise.
    [[nodiscard]] std::optional<TokenKind> lookup(std::string_view lexeme) const
    {
        auto it = map_.find(std::string(lexeme));
        if (it != map_.end())
            return it->second;
        return std::nullopt;
    }

    /// @brief Look up a keyword, returning a default if not found.
    /// @param lexeme The lexeme to look up.
    /// @param defaultKind The default token kind if not found.
    /// @return The token kind if found, defaultKind otherwise.
    [[nodiscard]] TokenKind lookupOr(std::string_view lexeme, TokenKind defaultKind) const
    {
        auto it = map_.find(std::string(lexeme));
        if (it != map_.end())
            return it->second;
        return defaultKind;
    }

    /// @brief Check if a lexeme is a keyword.
    [[nodiscard]] bool contains(std::string_view lexeme) const
    {
        return map_.find(std::string(lexeme)) != map_.end();
    }

    /// @brief Get the number of keywords in the map.
    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

    /// @brief Check if the map is empty.
    [[nodiscard]] bool empty() const noexcept { return map_.empty(); }

  private:
    std::unordered_map<std::string, TokenKind> map_;
};

} // namespace il::frontends::common::keyword_table
