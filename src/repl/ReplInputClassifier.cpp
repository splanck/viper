//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplInputClassifier.cpp
// Purpose: Implementation of REPL input classification for Zia.
// Key invariants:
//   - Braces/brackets inside string literals are not counted.
//   - Escape sequences inside strings are handled (\" does not end string).
// Ownership/Lifetime:
//   - Stateless; all classification is per-call.
// Links: src/repl/ReplInputClassifier.hpp
//
//===----------------------------------------------------------------------===//

#include "ReplInputClassifier.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace viper::repl {

// ---------------------------------------------------------------------------
// Helpers shared by Zia and BASIC classifiers
// ---------------------------------------------------------------------------

/// @brief Check if all characters in the string are whitespace.
static bool isAllWhitespace(const std::string &input) {
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

/// @brief Find the first non-whitespace character position.
static size_t findFirstNonSpace(const std::string &input) {
    size_t pos = 0;
    while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos])))
        ++pos;
    return pos;
}

/// @brief Case-insensitive comparison of a substring.
static bool matchKeywordCI(const std::string &line, size_t pos, const char *kw) {
    size_t kwLen = std::strlen(kw);
    if (pos + kwLen > line.size())
        return false;
    for (size_t i = 0; i < kwLen; ++i) {
        if (std::toupper(static_cast<unsigned char>(line[pos + i])) !=
            std::toupper(static_cast<unsigned char>(kw[i])))
            return false;
    }
    // Must be followed by whitespace, '(', or end of string (word boundary)
    if (pos + kwLen < line.size()) {
        char next = line[pos + kwLen];
        return std::isspace(static_cast<unsigned char>(next)) || next == '(' || next == ':';
    }
    return true;
}

// ---------------------------------------------------------------------------
// Zia classifier (bracket depth)
// ---------------------------------------------------------------------------

InputKind ReplInputClassifier::classify(const std::string &input) {
    if (isAllWhitespace(input))
        return InputKind::Empty;

    size_t firstNonSpace = findFirstNonSpace(input);
    if (firstNonSpace < input.size() && input[firstNonSpace] == '.')
        return InputKind::MetaCommand;

    int braceDepth = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    bool inString = false;
    bool escape = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\' && inString) {
            escape = true;
            continue;
        }

        if (c == '"') {
            inString = !inString;
            continue;
        }

        if (inString)
            continue;

        // Skip single-line comments
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '/')
            break;

        switch (c) {
            case '{':
                ++braceDepth;
                break;
            case '}':
                --braceDepth;
                break;
            case '(':
                ++parenDepth;
                break;
            case ')':
                --parenDepth;
                break;
            case '[':
                ++bracketDepth;
                break;
            case ']':
                --bracketDepth;
                break;
            default:
                break;
        }
    }

    if (braceDepth > 0 || parenDepth > 0 || bracketDepth > 0)
        return InputKind::Incomplete;

    return InputKind::Complete;
}

// ---------------------------------------------------------------------------
// BASIC classifier (block keyword tracking)
// ---------------------------------------------------------------------------

InputKind ReplInputClassifier::classifyBasic(const std::string &input) {
    if (isAllWhitespace(input))
        return InputKind::Empty;

    size_t firstNonSpace = findFirstNonSpace(input);
    if (firstNonSpace < input.size() && input[firstNonSpace] == '.')
        return InputKind::MetaCommand;

    // Split input into lines and track block depth via keyword matching.
    // Openers increment depth; closers decrement.
    int blockDepth = 0;

    size_t lineStart = 0;
    while (lineStart <= input.size()) {
        size_t lineEnd = input.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = input.size();

        std::string line = input.substr(lineStart, lineEnd - lineStart);

        // Strip trailing whitespace and comments (REM or ')
        size_t commentPos = std::string::npos;
        bool inStr = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"')
                inStr = !inStr;
            if (!inStr && line[i] == '\'') {
                commentPos = i;
                break;
            }
        }
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);

        // Trim trailing whitespace
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();

        // Find first non-whitespace token position
        size_t pos = findFirstNonSpace(line);
        if (pos >= line.size()) {
            lineStart = lineEnd + 1;
            continue;
        }

        // Check for block closers first (they reduce depth)
        // "END IF", "END SUB", "END FUNCTION", "END CLASS", "END TYPE",
        // "END SELECT", "END TRY", "END PROPERTY", "END NAMESPACE"
        if (matchKeywordCI(line, pos, "END")) {
            size_t afterEnd = pos + 3;
            while (afterEnd < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[afterEnd])))
                ++afterEnd;

            if (matchKeywordCI(line, afterEnd, "IF") || matchKeywordCI(line, afterEnd, "SUB") ||
                matchKeywordCI(line, afterEnd, "FUNCTION") ||
                matchKeywordCI(line, afterEnd, "CLASS") || matchKeywordCI(line, afterEnd, "TYPE") ||
                matchKeywordCI(line, afterEnd, "SELECT") || matchKeywordCI(line, afterEnd, "TRY") ||
                matchKeywordCI(line, afterEnd, "PROPERTY") ||
                matchKeywordCI(line, afterEnd, "NAMESPACE") ||
                matchKeywordCI(line, afterEnd, "GET") || matchKeywordCI(line, afterEnd, "SET")) {
                --blockDepth;
                lineStart = lineEnd + 1;
                continue;
            }
        }

        // "NEXT" closes FOR
        if (matchKeywordCI(line, pos, "NEXT")) {
            --blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // "WEND" closes WHILE
        if (matchKeywordCI(line, pos, "WEND")) {
            --blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // "LOOP" closes DO
        if (matchKeywordCI(line, pos, "LOOP")) {
            --blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // Check for block openers (they increase depth)

        // SUB / FUNCTION (multi-line definitions)
        if (matchKeywordCI(line, pos, "SUB") || matchKeywordCI(line, pos, "FUNCTION")) {
            ++blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // IF ... THEN — multi-line if THEN is at end of line with nothing after
        if (matchKeywordCI(line, pos, "IF")) {
            // Find THEN keyword
            bool foundThen = false;
            for (size_t i = pos + 2; i < line.size(); ++i) {
                if (matchKeywordCI(line, i, "THEN")) {
                    // Check if there's anything after THEN
                    size_t afterThen = i + 4;
                    while (afterThen < line.size() &&
                           std::isspace(static_cast<unsigned char>(line[afterThen])))
                        ++afterThen;
                    if (afterThen >= line.size()) {
                        // Nothing after THEN → multi-line IF
                        ++blockDepth;
                    }
                    // else: single-line IF (THEN <statement>) → no depth change
                    foundThen = true;
                    break;
                }
            }
            if (!foundThen) {
                // IF without THEN on same line → multi-line (continuation)
                ++blockDepth;
            }
            lineStart = lineEnd + 1;
            continue;
        }

        // DO (opens a DO/LOOP block)
        if (matchKeywordCI(line, pos, "DO")) {
            ++blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // WHILE (opens a WHILE/WEND block)
        if (matchKeywordCI(line, pos, "WHILE")) {
            ++blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // FOR ... TO ... (opens a FOR/NEXT block)
        if (matchKeywordCI(line, pos, "FOR")) {
            ++blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // SELECT CASE
        if (matchKeywordCI(line, pos, "SELECT")) {
            ++blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        // CLASS / TYPE / TRY / PROPERTY / NAMESPACE
        if (matchKeywordCI(line, pos, "CLASS") || matchKeywordCI(line, pos, "TYPE") ||
            matchKeywordCI(line, pos, "TRY") || matchKeywordCI(line, pos, "PROPERTY") ||
            matchKeywordCI(line, pos, "NAMESPACE")) {
            ++blockDepth;
            lineStart = lineEnd + 1;
            continue;
        }

        lineStart = lineEnd + 1;
    }

    if (blockDepth > 0)
        return InputKind::Incomplete;

    return InputKind::Complete;
}

} // namespace viper::repl
