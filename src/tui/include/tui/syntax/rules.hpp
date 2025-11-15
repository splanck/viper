// tui/include/tui/syntax/rules.hpp
// @brief Regex-based syntax highlighting rules and per-line cache.
// @invariant Cached spans are invalidated when the corresponding line changes.
// @ownership SyntaxRuleSet owns rule patterns and span cache.
#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "tui/render/screen.hpp"

namespace viper::tui::syntax
{

/// @brief Mapping of regex pattern to style.
struct SyntaxRule
{
    std::regex pattern;  ///< Regular expression to match.
    render::Style style; ///< Style applied to matches.
};

/// @brief Highlighted span within a line.
struct Span
{
    std::size_t start{0};  ///< Byte offset within line.
    std::size_t length{0}; ///< Length in bytes.
    render::Style style{}; ///< Style for the span.
};

/// @brief Set of syntax rules with per-line caching.
class SyntaxRuleSet
{
  public:
    /// @brief Load rules from a JSON file.
    /// @return True on success.
    bool loadFromFile(const std::string &path);

    /// @brief Get spans for a line, computing and caching on first request.
    const std::vector<Span> &spans(std::size_t lineNo, const std::string &line);

    /// @brief Invalidate cached spans for a specific line.
    void invalidate(std::size_t lineNo);

  private:
    std::vector<SyntaxRule> rules_{};
    std::unordered_map<std::size_t, std::pair<std::string, std::vector<Span>>> cache_{};
};

} // namespace viper::tui::syntax
