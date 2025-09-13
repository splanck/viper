// tui/src/text/search.cpp
// @brief TextBuffer search implementations for literal and regex queries.
// @invariant Limits searches to reasonable buffer size and handles regex errors.
// @ownership Functions borrow TextBuffer; matches returned by value.

#include "tui/text/search.hpp"

#include <regex>

namespace viper::tui::text
{
namespace
{
constexpr size_t kMaxSearchSize = 1 << 20; // 1MB cap
}

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
