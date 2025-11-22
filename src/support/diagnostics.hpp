//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/diagnostics.hpp
// Purpose: Declares diagnostic engine for errors and warnings. 
// Key invariants: None.
// Ownership/Lifetime: Engine owns collected diagnostics.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "source_location.hpp"
#include <ostream>
#include <string>
#include <vector>

/// @brief Records diagnostics and prints them later.
/// @invariant Counts reflect reported diagnostics.
/// @ownership Owns stored diagnostic messages.
namespace il::support
{

class SourceManager;

/// @brief Severity levels for diagnostics.
enum class Severity
{
    Note,
    Warning,
    Error
};

/// @brief Single diagnostic message with location.
struct Diagnostic
{
    Severity severity;   ///< Message severity
    std::string message; ///< Human-readable text
    SourceLoc loc;       ///< Optional source location
};

/// @brief Collects diagnostics and prints them in order.
class DiagnosticEngine
{
  public:
    /// @brief Record diagnostic @p d.
    /// @param d Diagnostic to store.
    void report(Diagnostic d);

    /// @brief Print all recorded diagnostics to stream @p os.
    /// @param os Output stream.
    /// @param sm Optional source manager for location info.
    void printAll(std::ostream &os, const SourceManager *sm = nullptr) const;

    /// @brief Number of errors reported.
    size_t errorCount() const;

    /// @brief Number of warnings reported.
    size_t warningCount() const;

  private:
    std::vector<Diagnostic> diags_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
};
} // namespace il::support
