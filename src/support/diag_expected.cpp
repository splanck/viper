/**
 * @file diag_expected.cpp
 * @brief Provides diagnostic-aware Expected helpers used throughout the codebase.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     These utilities adapt diagnostics to the `Expected` error-handling pattern
 *     and expose shared formatting helpers used by diagnostic printing.
 */

#include "diag_expected.hpp"

namespace il::support
{
/**
 * @brief Constructs an `Expected<void>` holding an error diagnostic.
 *
 * The constructor stores the diagnostic in the optional error payload, leaving
 * the object in a failure state.
 *
 * @param diag Diagnostic describing the failure.
 */
Expected<void>::Expected(Diag diag) : error_(std::move(diag))
{
}

/**
 * @brief Indicates whether the Expected contains a success value.
 *
 * Because the success variant carries no payload for `void`, the method simply
 * checks whether an error diagnostic is present.
 *
 * @return `true` when no error is stored.
 */
bool Expected<void>::hasValue() const
{
    return !error_.has_value();
}

/**
 * @brief Enables implicit boolean tests to check for success.
 *
 * Delegates to `hasValue()` so that idioms like `if (expected)` reflect whether
 * an error diagnostic was recorded.
 *
 * @return `true` when the Expected represents success.
 */
Expected<void>::operator bool() const
{
    return hasValue();
}

/**
 * @brief Retrieves the diagnostic describing the failure.
 *
 * Callers must ensure the Expected holds an error before invoking this method.
 * The diagnostic is returned by reference to avoid unnecessary copies.
 *
 * @return Reference to the stored diagnostic.
 */
const Diag &Expected<void>::error() const &
{
    return *error_;
}

namespace detail
{
/**
 * @brief Converts a severity enum into a printable string.
 *
 * The function matches the enumeration against the supported severities and
 * produces lowercase strings used in diagnostic output.  An empty string is
 * returned if an unknown severity is encountered, which should not happen in
 * normal operation.
 *
 * @param severity Diagnostic severity to format.
 * @return String literal describing the severity.
 */
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

/**
 * @brief Creates an error diagnostic with the supplied message and location.
 *
 * The helper centralizes construction so that error diagnostics consistently
 * use severity `Error` and capture the provided source location.
 *
 * @param loc Optional source location associated with the error.
 * @param msg Human-readable message text.
 * @return Diagnostic marked with severity `Error`.
 */
Diag makeError(SourceLoc loc, std::string msg)
{
    return Diag{Severity::Error, std::move(msg), loc};
}

/**
 * @brief Formats a diagnostic and writes it to an output stream.
 *
 * The printer optionally consults a `SourceManager` to translate file
 * identifiers into canonical paths.  When a valid location is present, it emits
 * `path:line:column:` as a prefix followed by the severity label and message.
 *
 * @param diag Diagnostic to print.
 * @param os Output stream receiving the formatted diagnostic.
 * @param sm Optional source manager for resolving file identifiers.
 */
void printDiag(const Diag &diag, std::ostream &os, const SourceManager *sm)
{
    if (diag.loc.isValid() && sm)
    {
        auto path = sm->getPath(diag.loc.file_id);
        os << path << ":" << diag.loc.line << ":" << diag.loc.column << ": ";
    }
    os << detail::diagSeverityToString(diag.severity) << ": " << diag.message
       << '\n';
}
} // namespace il::support
