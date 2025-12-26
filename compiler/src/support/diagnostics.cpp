//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

/// @file
/// @brief Houses the diagnostic aggregation engine shared across front-ends.
/// @details `il::support::DiagnosticEngine` acts as the canonical collector for
///          compiler diagnostics.  Translation units inject new diagnostics via
///          @ref report(), and tools can later print or query summary counts for
///          gating pipelines.  Centralising this logic ensures consistent
///          messaging independent of the originating subsystem.

#include "diagnostics.hpp"
#include "diag_expected.hpp"
#include "source_manager.hpp"

namespace il::support
{
/// @brief Record a diagnostic and update severity counters.
///
/// @details Diagnostics are appended to the internal vector for later
///          enumeration.  Error and warning severities update dedicated counters
///          so clients can make decisions (for example, halting compilation
///          after errors).  The diagnostic is moved into the engine, preserving
///          ownership semantics while avoiding copies.
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
/// @details Delegates to `printDiag()` so message formatting remains
///          centralized.  When a SourceManager is provided, file identifiers are
///          resolved to normalized paths before printing.  The helper emits a
///          trailing newline for each diagnostic, mirroring the behaviour of the
///          individual printer.
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
/// @details The counter increments on each call to @ref report() where the
///          severity equals @ref Severity::Error.  The value never decreases,
///          providing an inexpensive way for tooling to enforce "no errors"
///          policies.
///
/// @return Count of error-severity diagnostics recorded so far.
size_t DiagnosticEngine::errorCount() const
{
    return errors_;
}

/// @brief Retrieve the number of diagnostics reported as warnings.
///
/// @details Mirrors @ref errorCount() but for @ref Severity::Warning, enabling
///          dashboards or CLI tools to summarise non-fatal issues.
///
/// @return Count of warning-severity diagnostics recorded so far.
size_t DiagnosticEngine::warningCount() const
{
    return warnings_;
}
} // namespace il::support
