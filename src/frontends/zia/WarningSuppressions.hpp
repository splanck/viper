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
#include "support/source_location.hpp"
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace il::frontends::zia
{

/// @brief Scans source text for @suppress directives and provides suppression queries.
class WarningSuppressions
{
  public:
    /// @brief Remove all recorded suppressions.
    void clear()
    {
        suppressions_.clear();
    }

    /// @brief Scan source text and extract all @suppress directives for a file.
    /// @param fileId SourceManager file identifier for the source text.
    /// @param source The full source text to scan.
    void scan(uint32_t fileId, std::string_view source)
    {
        if (fileId == 0)
            return;

        auto &fileSuppressions = suppressions_[fileId];
        uint32_t lineNum = 1;
        size_t pos = 0;

        while (pos < source.size())
        {
            // Find end of current line
            size_t eol = source.find('\n', pos);
            if (eol == std::string_view::npos)
                eol = source.size();

            std::string_view line = source.substr(pos, eol - pos);
            parseLine(fileSuppressions, line, lineNum);

            pos = eol + 1;
            lineNum++;
        }
    }

    /// @brief Check if a warning is suppressed at a given source location.
    /// @details A `// @suppress(Wxxx)` on line N suppresses warnings on lines N and N+1.
    /// @param code The warning code to check.
    /// @param loc The source location where the warning would be emitted.
    /// @return true if the warning is suppressed.
    bool isSuppressed(WarningCode code, const il::support::SourceLoc &loc) const
    {
        if (!loc.hasFile() || !loc.hasLine())
            return false;

        auto fileIt = suppressions_.find(loc.file_id);
        if (fileIt == suppressions_.end())
            return false;

        // Check if suppressed on this line (inline suppress) or preceding line
        uint32_t line = loc.line;
        for (uint32_t checkLine = (line > 0 ? line - 1 : 0); checkLine <= line; checkLine++)
        {
            auto it = fileIt->second.find(checkLine);
            if (it != fileIt->second.end() && it->second.count(code))
                return true;
        }
        return false;
    }

  private:
    /// @brief Parse a single line for @suppress directives.
    void parseLine(std::unordered_map<uint32_t, std::unordered_set<WarningCode>> &fileSuppressions,
                   std::string_view line,
                   uint32_t lineNum)
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
                    fileSuppressions[lineNum].insert(*code);
                }
            }

            p = tokenEnd + 1;
        }
    }

    /// @brief Map from file id to line-local suppressed warning codes.
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::unordered_set<WarningCode>>>
        suppressions_;
};

} // namespace il::frontends::zia
