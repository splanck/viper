//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/DiagnosticFormatter.hpp
// Purpose: Language-agnostic diagnostic formatting utilities.
// Key invariants:
//   - Source line extraction is O(n) in line count via linear scan
//   - Caret generation is safe for zero-column locations (defaults to 1 caret)
// Ownership/Lifetime:
//   - Header-only, stateless utility functions
// Links: frontends/common/DiagnosticHelpers.hpp, support/diagnostics.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <ostream>
#include <string>
#include <unordered_map>

namespace il::frontends::common::diag_fmt {

/// @brief Convert a severity level to a human-readable string.
/// @param s Severity to convert.
/// @return Null-terminated severity name ("error", "warning", "note").
[[nodiscard]] inline const char *severityToString(il::support::Severity s) noexcept {
    using il::support::Severity;
    switch (s) {
        case Severity::Note:
            return "note";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
            return "error";
    }
    return "";
}

/// @brief Extract a specific line from cached source text.
/// @param source The full source text of a file.
/// @param line 1-based line number to extract.
/// @return The line contents without trailing newline, or empty if unavailable.
///
/// @details Performs a linear scan through the source to find the requested
///          line. Returns empty string for line 0 or lines past the end.
[[nodiscard]] inline std::string getSourceLine(const std::string &source, uint32_t line) {
    if (line == 0)
        return {};

    size_t start = 0;
    for (uint32_t l = 1; l < line; ++l) {
        size_t pos = source.find('\n', start);
        if (pos == std::string::npos)
            return {};
        start = pos + 1;
    }
    size_t end = source.find('\n', start);
    if (end == std::string::npos)
        end = source.size();
    return source.substr(start, end - start);
}

/// @brief Format a source location as "path:line:col" for diagnostic output.
/// @param sm Source manager providing file path lookups.
/// @param loc Source location to format.
/// @param skipLine Callable(uint32_t line) -> bool that returns true if the line
///        should be omitted from the format string (e.g., unlabeled BASIC lines).
/// @return Formatted "path:line:col" string, or empty if path is unavailable.
template <typename SkipLineFn>
[[nodiscard]] inline std::string formatLocation(const il::support::SourceManager &sm,
                                                il::support::SourceLoc loc,
                                                SkipLineFn skipLine) {
    if (loc.file_id == 0)
        return {};
    auto path = sm.getPath(loc.file_id);
    if (path.empty())
        return {};
    std::string result(path);
    if (!skipLine(loc.line)) {
        result += ':';
        result += std::to_string(loc.line);
        if (loc.column != 0) {
            result += ':';
            result += std::to_string(loc.column);
        }
    }
    return result;
}

/// @brief Format a source location as "path:line:col" for diagnostic output.
/// @details Overload that never skips lines.
/// @param sm Source manager providing file path lookups.
/// @param loc Source location to format.
/// @return Formatted "path:line:col" string, or empty if path is unavailable.
[[nodiscard]] inline std::string formatLocation(const il::support::SourceManager &sm,
                                                il::support::SourceLoc loc) {
    return formatLocation(sm, loc, [](uint32_t) { return false; });
}

/// @brief Write a diagnostic entry with caret annotation to an output stream.
/// @param os Output stream to write to.
/// @param severity Diagnostic severity level.
/// @param code Error code string (e.g., "B1001", "V1000").
/// @param message Human-readable diagnostic message.
/// @param locationStr Pre-formatted "path:line:col" string (may be empty).
/// @param sourceLine The source line text for context (may be empty).
/// @param column 1-based column for caret positioning.
/// @param length Number of characters to underline (0 defaults to 1 caret).
inline void printDiagnostic(std::ostream &os,
                            il::support::Severity severity,
                            const std::string &code,
                            const std::string &message,
                            const std::string &locationStr,
                            const std::string &sourceLine,
                            uint32_t column,
                            uint32_t length) {
    if (!locationStr.empty())
        os << locationStr << ": ";
    os << severityToString(severity) << '[' << code << "]: " << message << '\n';
    if (!sourceLine.empty()) {
        os << sourceLine << '\n';
        uint32_t caretLen = length == 0 ? 1 : length;
        uint32_t indent = column > 0 ? column - 1 : 0;
        os << std::string(indent, ' ') << std::string(caretLen, '^') << '\n';
    }
}

/// @brief Format a "path:line" string for a source location.
/// @param sm Source manager providing file path lookups.
/// @param loc Source location to format.
/// @return "path:line" string, or empty if path or line are unavailable.
[[nodiscard]] inline std::string formatFileLine(const il::support::SourceManager &sm,
                                                il::support::SourceLoc loc) {
    if (!loc.hasFile() || !loc.hasLine())
        return {};
    std::string path = std::string(sm.getPath(loc.file_id));
    if (path.empty())
        return {};
    return path + ":" + std::to_string(loc.line);
}

} // namespace il::frontends::common::diag_fmt
