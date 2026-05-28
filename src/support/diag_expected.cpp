//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/support/diag_expected.cpp
// Purpose: Provide the Expected<void>-based diagnostic infrastructure shared by
//          front ends, passes, and tooling.
// Key invariants: Success is represented by an empty optional diagnostic; every
//                 failure stores a fully-populated @ref Diag.  Severity strings
//                 must remain lowercase to match existing command-line output.
// Ownership/Lifetime: Expected instances own their diagnostic payloads by value;
//                     printing helpers borrow output streams and optional source
//                     managers supplied by the caller.
// Links: src/support/diag_expected.hpp, docs/codemap.md#support-library
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Supplies the `Expected<void>` helpers specialized for diagnostics.
/// @details The support layer leans heavily on `Expected<void>` to propagate
///          recoverable failures.  This translation unit gathers all diagnostic
///          focused utilities—constructors, severity conversions, and printers—
///          so clients get a coherent experience when emitting or displaying
///          errors from disparate subsystems.

#include "diag_expected.hpp"

#include <algorithm>
#include <string_view>

namespace il::support {
namespace {
/// @brief Calculate a same-line diagnostic underline length.
uint32_t sameLineRangeLength(const Diag &diag) {
    if (!diag.range.isValid() || diag.range.begin.file_id != diag.loc.file_id ||
        diag.range.begin.line != diag.loc.line || diag.range.end.line != diag.loc.line ||
        diag.range.begin.column == 0 || diag.range.end.column <= diag.range.begin.column) {
        return 1;
    }
    return diag.range.end.column - diag.range.begin.column;
}

/// @brief Print the source line and caret marker for a diagnostic-like location.
void printSourceSnippet(SourceLoc loc,
                        uint32_t underlineLength,
                        std::ostream &os,
                        const SourceManager *sm) {
    if (!sm || loc.file_id == 0 || loc.line == 0)
        return;

    auto srcLine = sm->getLine(loc.file_id, loc.line);
    if (srcLine.empty())
        return;

    std::string lineNumStr = std::to_string(loc.line);
    std::string gutter(lineNumStr.size(), ' ');

    os << ' ' << lineNumStr << " | " << srcLine << '\n';

    if (loc.column == 0 || loc.column > srcLine.size() + 1)
        return;

    os << ' ' << gutter << " | ";
    for (uint32_t i = 1; i < loc.column; ++i) {
        if (i <= srcLine.size() && srcLine[i - 1] == '\t')
            os << '\t';
        else
            os << ' ';
    }
    const uint32_t maxLength = static_cast<uint32_t>(
        srcLine.size() + 1 >= loc.column ? srcLine.size() + 1 - loc.column : 1);
    const uint32_t caretCount = std::max<uint32_t>(1, std::min(underlineLength, maxLength));
    os << '^';
    for (uint32_t i = 1; i < caretCount; ++i)
        os << '~';
    os << '\n';
}

/// @brief Print a diagnostic header without attached notes.
void printDiagHeader(const Diag &diag, std::ostream &os, const SourceManager *sm) {
    if (sm && diag.loc.file_id != 0) {
        auto path = sm->getPath(diag.loc.file_id);
        if (!path.empty()) {
            os << path;
            if (diag.loc.line != 0) {
                os << ':' << diag.loc.line;
                if (diag.loc.column != 0) {
                    os << ':' << diag.loc.column;
                }
            }
            os << ": ";
        }
    }
    os << detail::diagSeverityToString(diag.severity);
    if (!diag.code.empty()) {
        os << '[' << diag.code << ']';
    }
    os << ": " << diag.message << '\n';
}

void printJsonEscaped(std::ostream &os, std::string_view text) {
    os << '"';
    for (unsigned char ch : text) {
        switch (ch) {
            case '"':
                os << "\\\"";
                break;
            case '\\':
                os << "\\\\";
                break;
            case '\b':
                os << "\\b";
                break;
            case '\f':
                os << "\\f";
                break;
            case '\n':
                os << "\\n";
                break;
            case '\r':
                os << "\\r";
                break;
            case '\t':
                os << "\\t";
                break;
            default:
                if (ch < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    os << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    os << static_cast<char>(ch);
                }
                break;
        }
    }
    os << '"';
}

void printJsonLoc(std::ostream &os, SourceLoc loc, const SourceManager *sm) {
    os << "\"location\":{";
    os << "\"file_id\":" << loc.file_id << ',';
    os << "\"line\":" << loc.line << ',';
    os << "\"column\":" << loc.column << ',';
    os << "\"file\":";
    if (sm && loc.file_id != 0) {
        printJsonEscaped(os, sm->getPath(loc.file_id));
    } else {
        os << "null";
    }
    os << '}';
}

void printJsonOptionalString(std::ostream &os, std::string_view text) {
    if (text.empty())
        os << "null";
    else
        printJsonEscaped(os, text);
}

void printJsonSourceLine(std::ostream &os, SourceLoc loc, const SourceManager *sm) {
    os << "\"source\":";
    if (!sm || loc.file_id == 0 || loc.line == 0) {
        os << "null";
        return;
    }
    const std::string line{sm->getLine(loc.file_id, loc.line)};
    if (line.empty()) {
        os << "null";
        return;
    }
    printJsonEscaped(os, line);
}

void printJsonRange(std::ostream &os, const SourceRange &range, const SourceManager *sm) {
    os << "\"range\":";
    if (!range.isValid()) {
        os << "null";
        return;
    }
    os << '{';
    os << "\"begin\":{";
    os << "\"file_id\":" << range.begin.file_id << ',';
    os << "\"line\":" << range.begin.line << ',';
    os << "\"column\":" << range.begin.column << ',';
    os << "\"file\":";
    if (sm && range.begin.file_id != 0) {
        printJsonEscaped(os, sm->getPath(range.begin.file_id));
    } else {
        os << "null";
    }
    os << "},\"end\":{";
    os << "\"file_id\":" << range.end.file_id << ',';
    os << "\"line\":" << range.end.line << ',';
    os << "\"column\":" << range.end.column << ',';
    os << "\"file\":";
    if (sm && range.end.file_id != 0) {
        printJsonEscaped(os, sm->getPath(range.end.file_id));
    } else {
        os << "null";
    }
    os << "}}";
}

void printJsonDiagObject(const Diag &diag, std::ostream &os, const SourceManager *sm) {
    os << '{';
    os << "\"severity\":";
    printJsonEscaped(os, detail::diagSeverityToString(diag.severity));
    os << ",\"code\":";
    if (diag.code.empty()) {
        os << "null";
    } else {
        printJsonEscaped(os, diag.code);
    }
    os << ",\"stage\":";
    printJsonOptionalString(os, diag.stage);
    os << ",\"message\":";
    printJsonEscaped(os, diag.message);
    os << ',';
    printJsonLoc(os, diag.loc, sm);
    os << ',';
    printJsonRange(os, diag.range, sm);
    os << ',';
    printJsonSourceLine(os, diag.loc, sm);
    os << ",\"help\":";
    printJsonOptionalString(os, diag.help);
    os << ",\"notes\":[";
    for (size_t index = 0; index < diag.notes.size(); ++index) {
        if (index != 0)
            os << ',';
        const auto &note = diag.notes[index];
        os << '{';
        os << "\"message\":";
        printJsonEscaped(os, note.message);
        os << ',';
        printJsonLoc(os, note.loc, sm);
        os << '}';
    }
    os << "],\"fixits\":[";
    for (size_t index = 0; index < diag.fixits.size(); ++index) {
        if (index != 0)
            os << ',';
        const auto &fixit = diag.fixits[index];
        os << '{';
        os << "\"message\":";
        printJsonOptionalString(os, fixit.message);
        os << ',';
        printJsonRange(os, fixit.range, sm);
        os << ",\"replacement\":";
        printJsonEscaped(os, fixit.replacement);
        os << '}';
    }
    os << "]}";
}
} // namespace

/// @brief Construct an Expected<void> that stores a diagnostic error state.
///
/// @details The constructor moves the provided diagnostic into the optional
///          error slot, thereby marking the instance as failed.  A
///          default-constructed `Expected` contains no diagnostic payload and
///          represents success.  Moving into the error slot avoids copies while
///          ensuring the caller relinquishes ownership of the diagnostic.
///
/// @param diag Diagnostic to transfer into the error payload.
Expected<void>::Expected(Diag diag) : error_(std::move(diag)) {}

/// @brief Report whether the Expected<void> represents a successful outcome.
///
/// @details Success is indicated by the absence of a stored diagnostic.  The
///          helper performs no side effects, allowing callers to branch on
///          success without extracting or modifying the payload.  It is the
///          canonical way to test the error flag prior to retrieving the
///          diagnostic via @ref error().
///
/// @return True if the instance holds no diagnostic (success), otherwise false.
bool Expected<void>::hasValue() const {
    return !error_.has_value();
}

/// @brief Allow Expected<void> to participate directly in boolean tests.
///
/// @details Delegates to @ref hasValue() so callers can write idiomatic
///          conditionals such as `if (auto ok = doThing())`.  The conversion is
///          explicit enough to avoid accidental narrowing yet terse enough to be
///          pleasant in control flow.
Expected<void>::operator bool() const {
    return hasValue();
}

/// @brief Access the diagnostic that describes the recorded failure.
///
/// @details Callers must ensure the `Expected` represents an error before
///          invoking this accessor; doing so is undefined behaviour otherwise.
///          The returned reference remains valid for the lifetime of the
///          `Expected` instance and allows callers to inspect severity, message
///          text, and locations without copying.
///
/// @return Reference to the stored diagnostic payload.
const Diag &Expected<void>::error() const & {
    return *error_;
}

namespace detail {
/// @brief Map a diagnostic severity to a lowercase string used for printing.
///
/// @details The helper keeps the conversion in one location so diagnostic
///          formatting stays consistent across the codebase.  Unrecognised
///          enumerators fall back to an empty string, allowing call sites to
///          continue emitting diagnostics even when future severities are added
///          but not yet handled.  New severity enumerators should extend this
///          switch to maintain predictable wording across command-line tools.
///
/// @param severity Severity enumeration value to translate.
/// @return Null-terminated string naming the severity level.
const char *diagSeverityToString(Severity severity) {
    switch (severity) {
        case Severity::Note:
            return "note";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
            return "error";
    }
    return "";
}
} // namespace detail

/// @brief Build an error diagnostic with the provided location and message.
///
/// @details This convenience function standardises the error severity used by
///          several call sites.  The location is optional and defaults to an
///          unknown value when not supplied by the caller, preserving the
///          invariant that missing source metadata is explicitly marked.  The
///          message string is moved into the diagnostic to avoid needless copies
///          when callers forward freshly constructed text.
///
/// @param loc Source location that triggered the diagnostic, or unknown.
/// @param msg Human-readable description of the problem.
/// @return Diagnostic populated with error severity and provided context.
Diag makeError(SourceLoc loc, std::string msg) {
    return Diag{Severity::Error, std::move(msg), loc, {}};
}

/// @brief Build an error diagnostic with the provided location, code, and message.
///
/// @details This convenience function creates a diagnostic with both a code and
///          message.  The code appears in the formatted output as `[CODE]` after
///          the severity level, enabling programmatic filtering while preserving
///          human readability.
///
/// @param loc Source location that triggered the diagnostic, or unknown.
/// @param code Diagnostic code for programmatic identification.
/// @param msg Human-readable description of the problem.
/// @return Diagnostic populated with error severity, code, and provided context.
Diag makeErrorWithCode(SourceLoc loc, std::string code, std::string msg) {
    return Diag{Severity::Error, std::move(msg), loc, std::move(code)};
}

/// @brief Print a diagnostic to the provided output stream.
///
/// @details The printer optionally queries a SourceManager to resolve file
///          identifiers into normalized paths.  When a valid location is
///          available the message is prefixed with
///          "<path>:<line>:<column>:" following the common compiler diagnostic
///          style.  The formatted severity string comes from
///          `detail::diagSeverityToString()` to keep wording consistent.
///
///          Canonical output format:
///            <path>:<line>:<column>: <severity>[<code>]: <message>
///          When no code is present:
///            <path>:<line>:<column>: <severity>: <message>
///
///          The function always emits a trailing newline so multiple diagnostics
///          appear as a contiguous block.
///
/// @param diag Diagnostic to render.
/// @param os Output stream receiving the textual representation.
/// @param sm Optional source manager for mapping file identifiers to paths.
void printDiag(const Diag &diag, std::ostream &os, const SourceManager *sm) {
    printDiagHeader(diag, os, sm);
    printSourceSnippet(diag.loc, sameLineRangeLength(diag), os, sm);

    for (const auto &note : diag.notes) {
        Diag noteDiag{Severity::Note, note.message, note.loc, {}};
        printDiagHeader(noteDiag, os, sm);
        printSourceSnippet(note.loc, 1, os, sm);
    }
}

void printDiagnosticsJson(const std::vector<Diag> &diagnostics,
                          std::ostream &os,
                          const SourceManager *sm) {
    os << "{\"diagnostics\":[";
    for (size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0)
            os << ',';
        printJsonDiagObject(diagnostics[index], os, sm);
    }
    os << "]}\n";
}

void printDiagJson(const Diag &diag, std::ostream &os, const SourceManager *sm) {
    std::vector<Diag> diagnostics;
    diagnostics.push_back(diag);
    printDiagnosticsJson(diagnostics, os, sm);
}
} // namespace il::support
