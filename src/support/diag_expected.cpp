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

namespace il::support
{
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
bool Expected<void>::hasValue() const
{
    return !error_.has_value();
}

/// @brief Allow Expected<void> to participate directly in boolean tests.
///
/// @details Delegates to @ref hasValue() so callers can write idiomatic
///          conditionals such as `if (auto ok = doThing())`.  The conversion is
///          explicit enough to avoid accidental narrowing yet terse enough to be
///          pleasant in control flow.
Expected<void>::operator bool() const
{
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
const Diag &Expected<void>::error() const &
{
    return *error_;
}

namespace detail
{
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
const char *diagSeverityToString(Severity severity)
{
    switch (severity)
    {
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
Diag makeError(SourceLoc loc, std::string msg)
{
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
Diag makeErrorWithCode(SourceLoc loc, std::string code, std::string msg)
{
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
void printDiag(const Diag &diag, std::ostream &os, const SourceManager *sm)
{
    if (sm && diag.loc.file_id != 0)
    {
        auto path = sm->getPath(diag.loc.file_id);
        if (!path.empty())
        {
            os << path;
            if (diag.loc.line != 0)
            {
                os << ':' << diag.loc.line;
                if (diag.loc.column != 0)
                {
                    os << ':' << diag.loc.column;
                }
            }
            os << ": ";
        }
    }
    os << detail::diagSeverityToString(diag.severity);
    if (!diag.code.empty())
    {
        os << '[' << diag.code << ']';
    }
    os << ": " << diag.message << '\n';

    // Source snippet with caret marker
    if (sm && diag.loc.file_id != 0 && diag.loc.line != 0)
    {
        auto srcLine = sm->getLine(diag.loc.file_id, diag.loc.line);
        if (!srcLine.empty())
        {
            // Line number gutter
            std::string lineNumStr = std::to_string(diag.loc.line);
            std::string gutter(lineNumStr.size(), ' ');

            os << ' ' << lineNumStr << " | " << srcLine << '\n';

            // Caret line
            if (diag.loc.column != 0 && diag.loc.column <= srcLine.size() + 1)
            {
                os << ' ' << gutter << " | ";
                // Preserve leading whitespace (tabs/spaces) for alignment
                for (uint32_t i = 1; i < diag.loc.column; ++i)
                {
                    if (i <= srcLine.size() && srcLine[i - 1] == '\t')
                        os << '\t';
                    else
                        os << ' ';
                }
                os << "^\n";
            }
        }
    }
}
} // namespace il::support
