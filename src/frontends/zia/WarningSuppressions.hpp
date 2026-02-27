//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file WarningSuppressions.hpp
/// @brief Inline comment-based warning suppression for Zia source files.
///
/// @details Pre-scans source text for `// @suppress(W001)` or
/// `// @suppress(unused-variable)` comments. A suppression on line N applies
/// to the statement on line N (same line) or N+1 (next line).
///
/// Syntax:
///   // @suppress(W001)
///   // @suppress(unused-variable)
///   // @suppress(W001, W005)        — multiple codes
///
/// @see Warnings.hpp — warning code definitions.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/Warnings.hpp"
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace il::frontends::zia
{

/// @brief Scans source text for @suppress directives and provides suppression queries.
class WarningSuppressions
{
  public:
    /// @brief Scan source text and extract all @suppress directives.
    /// @param source The full source text to scan.
    void scan(std::string_view source)
    {
        suppressions_.clear();
        uint32_t lineNum = 1;
        size_t pos = 0;

        while (pos < source.size())
        {
            // Find end of current line
            size_t eol = source.find('\n', pos);
            if (eol == std::string_view::npos)
                eol = source.size();

            std::string_view line = source.substr(pos, eol - pos);
            parseLine(line, lineNum);

            pos = eol + 1;
            lineNum++;
        }
    }

    /// @brief Check if a warning is suppressed at a given line.
    /// @details A `// @suppress(Wxxx)` on line N suppresses warnings on lines N and N+1.
    /// @param code The warning code to check.
    /// @param line The 1-based line number where the warning would be emitted.
    /// @return true if the warning is suppressed.
    bool isSuppressed(WarningCode code, uint32_t line) const
    {
        // Check if suppressed on this line (inline suppress) or preceding line
        for (uint32_t checkLine = (line > 0 ? line - 1 : 0); checkLine <= line; checkLine++)
        {
            auto it = suppressions_.find(checkLine);
            if (it != suppressions_.end() && it->second.count(code))
                return true;
        }
        return false;
    }

  private:
    /// @brief Parse a single line for @suppress directives.
    void parseLine(std::string_view line, uint32_t lineNum)
    {
        // Look for "// @suppress(" anywhere on the line
        auto commentPos = line.find("// @suppress(");
        if (commentPos == std::string_view::npos)
            return;

        size_t start = commentPos + 13; // length of "// @suppress("
        auto closePos = line.find(')', start);
        if (closePos == std::string_view::npos)
            return;

        // Extract the content between parens: "W001, W005" or "unused-variable"
        std::string_view content = line.substr(start, closePos - start);

        // Split by comma and parse each code
        size_t p = 0;
        while (p < content.size())
        {
            // Skip whitespace
            while (p < content.size() && (content[p] == ' ' || content[p] == '\t'))
                p++;
            if (p >= content.size())
                break;

            // Find end of token (next comma or end)
            size_t tokenEnd = content.find(',', p);
            if (tokenEnd == std::string_view::npos)
                tokenEnd = content.size();

            // Trim trailing whitespace
            size_t end = tokenEnd;
            while (end > p && (content[end - 1] == ' ' || content[end - 1] == '\t'))
                end--;

            if (end > p)
            {
                std::string_view token = content.substr(p, end - p);
                if (auto code = parseWarningCode(token))
                {
                    suppressions_[lineNum].insert(*code);
                }
            }

            p = tokenEnd + 1;
        }
    }

    /// @brief Map from line number to set of suppressed warning codes on that line.
    std::unordered_map<uint32_t, std::unordered_set<WarningCode>> suppressions_;
};

} // namespace il::frontends::zia
