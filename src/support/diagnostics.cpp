//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the DiagnosticEngine that aggregates diagnostics emitted by the
// compiler front-ends and support utilities.  The engine stores the diagnostics
// for later inspection and offers helpers for printing them in a consistent
// format while tracking counts of warnings and errors.
//
//===----------------------------------------------------------------------===//

#include "diagnostics.hpp"
#include "diag_expected.hpp"
#include "source_manager.hpp"

namespace il::support
{
/// @brief Record a diagnostic and update severity counters.
///
/// Diagnostics are appended to the internal vector for later enumeration.
/// Error and warning severities update dedicated counters so clients can make
/// decisions (for example, halting compilation after errors).
///
/// @param d Diagnostic to store (moved into the engine).
void DiagnosticEngine::report(Diagnostic d)
{
    if (d.severity == Severity::Error)
        ++errors_;
    else if (d.severity == Severity::Warning)
        ++warnings_;
    diags_.push_back(std::move(d));
}

/// @brief Print all recorded diagnostics in insertion order.
///
/// Delegates to printDiag() so message formatting remains centralized.  When a
/// SourceManager is provided, file identifiers are resolved to normalized paths.
///
/// @param os Output stream receiving diagnostic text.
/// @param sm Optional source manager for resolving locations.
void DiagnosticEngine::printAll(std::ostream &os, const SourceManager *sm) const
{
    for (const auto &d : diags_)
    {
        printDiag(d, os, sm);
    }
}

/// @brief Retrieve the number of diagnostics reported as errors.
///
/// @return Count of error-severity diagnostics recorded so far.
size_t DiagnosticEngine::errorCount() const
{
    return errors_;
}

/// @brief Retrieve the number of diagnostics reported as warnings.
///
/// @return Count of warning-severity diagnostics recorded so far.
size_t DiagnosticEngine::warningCount() const
{
    return warnings_;
}
} // namespace il::support
