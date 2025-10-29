//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides literal and regular-expression search utilities for the terminal UI
// text buffer.  The implementation keeps runtime safeguards in place (size caps,
// exception handling) so interactive searches cannot stall the UI or surface
// uncaught regex errors to users.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Search helpers for @ref viper::tui::text::TextBuffer.
/// @details The functions offer simple literal scans and regular-expression
///          matches while constraining the scanned text to a configurable limit
///          to keep latency predictable.  Results are returned by value so the
///          caller can store or display them freely.

#include "tui/text/search.hpp"

#include <regex>

namespace viper::tui::text
{
namespace
{
/// @brief Maximum number of characters a search considers before truncating.
/// @details Keeps regex operations bounded so pathological patterns cannot freeze
///          the UI.  One megabyte is large enough for typical buffers yet small
///          enough for interactive latency.
constexpr size_t kMaxSearchSize = 1 << 20; // 1MB cap
} // namespace

/// @brief Locate every match for @p query within @p buf.
/// @details Performs a literal scan when @p useRegex is false and otherwise uses
///          @c std::regex to discover matches.  The buffer is truncated to
///          @ref kMaxSearchSize characters before searching to keep runtime costs
///          predictable.  Regex errors result in an empty match list rather than
///          propagating exceptions to the caller.
std::vector<Match> findAll(const TextBuffer &buf, std::string_view query, bool useRegex)
{
    std::vector<Match> hits;
    if (query.empty())
    {
        return hits;
    }
    std::string hay = buf.str();
    if (hay.size() > kMaxSearchSize)
    {
        hay.resize(kMaxSearchSize);
    }
    if (!useRegex)
    {
        size_t pos = 0;
        while ((pos = hay.find(query, pos)) != std::string::npos)
        {
            hits.push_back(Match{pos, query.size()});
            pos += query.size() > 0 ? query.size() : 1;
        }
        return hits;
    }
    try
    {
        std::regex re{std::string(query)};
        auto begin = std::sregex_iterator(hay.begin(), hay.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it)
        {
            hits.push_back(
                Match{static_cast<size_t>(it->position()), static_cast<size_t>(it->length())});
        }
    }
    catch (const std::regex_error &)
    {
        return {};
    }
    return hits;
}

/// @brief Find the next match starting at @p from.
/// @details Mirrors @ref findAll but stops at the first occurrence on or after
///          the requested offset.  Literal searches delegate to
///          @c std::string::find, while regex searches reuse the same truncated
///          buffer strategy.  Regex failures yield @c std::nullopt so the caller
///          can continue gracefully.
std::optional<Match> findNext(const TextBuffer &buf,
                              std::string_view query,
                              size_t from,
                              bool useRegex)
{
    if (query.empty())
    {
        return std::nullopt;
    }
    std::string hay = buf.str();
    if (hay.size() > kMaxSearchSize)
    {
        hay.resize(kMaxSearchSize);
    }
    if (!useRegex)
    {
        size_t pos = hay.find(query, from);
        if (pos != std::string::npos)
        {
            return Match{pos, query.size()};
        }
        return std::nullopt;
    }
    try
    {
        std::regex re{std::string(query)};
        std::cmatch m;
        const char *start = hay.c_str() + std::min(from, hay.size());
        if (std::regex_search(start, hay.c_str() + hay.size(), m, re))
        {
            return Match{static_cast<size_t>(m.position()) + std::min(from, hay.size()),
                         static_cast<size_t>(m.length())};
        }
    }
    catch (const std::regex_error &)
    {
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace viper::tui::text
