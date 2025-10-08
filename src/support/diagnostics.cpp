/**
 * @file diagnostics.cpp
 * @brief Implements the diagnostic engine responsible for collecting messages.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     The diagnostic engine aggregates messages emitted throughout compilation
 *     and keeps track of severity counts.  Diagnostics are stored until callers
 *     explicitly print or inspect them.
 */

#include "diagnostics.hpp"
#include "diag_expected.hpp"
#include "source_manager.hpp"

namespace il::support
{
/**
 * @brief Adds a diagnostic to the engine and updates severity counters.
 *
 * The diagnostic is appended to the internal vector for later inspection.  The
 * method increments the error or warning counter depending on the diagnostic's
 * severity, leaving other severities (e.g. notes) unchanged.
 *
 * @param d Diagnostic to record; moved into the engine's storage.
 */
void DiagnosticEngine::report(Diagnostic d)
{
    if (d.severity == Severity::Error)
        ++errors_;
    else if (d.severity == Severity::Warning)
        ++warnings_;
    diags_.push_back(std::move(d));
}

/**
 * @brief Writes all stored diagnostics to the provided output stream.
 *
 * The function iterates through the recorded messages and delegates formatting
 * to `printDiag`, which knows how to incorporate optional source locations.  If
 * a `SourceManager` pointer is supplied, it is passed along so that file paths
 * can be resolved for diagnostics with location information.
 *
 * @param os Output stream that receives the formatted diagnostics.
 * @param sm Optional source manager used to translate file identifiers.
 */
void DiagnosticEngine::printAll(std::ostream &os, const SourceManager *sm) const
{
    for (const auto &d : diags_)
    {
        printDiag(d, os, sm);
    }
}

/**
 * @brief Returns the number of error-severity diagnostics recorded so far.
 *
 * The count is incremented whenever `report` receives a diagnostic classified
 * as an error and never decreases unless the engine itself is discarded.
 *
 * @return Number of stored diagnostics with severity `Error`.
 */
size_t DiagnosticEngine::errorCount() const
{
    return errors_;
}

/**
 * @brief Returns the number of warning-severity diagnostics recorded so far.
 *
 * The count is maintained alongside the error count and reflects how many
 * warnings have been reported since the engine was created or last cleared.
 *
 * @return Number of stored diagnostics with severity `Warning`.
 */
size_t DiagnosticEngine::warningCount() const
{
    return warnings_;
}
} // namespace il::support
