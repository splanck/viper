//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/diagnostics.hpp
// Purpose: Declares diagnostic engine for errors and warnings.
// Key invariants: errorCount()/warningCount() always equal the number of stored
//                 diagnostics at each severity; printAll preserves report order.
// Ownership/Lifetime: Engine owns collected diagnostics.
// Links: docs/internals/codemap.md
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
namespace il::support {

class SourceManager;

/// @brief Severity levels for diagnostics.
enum class Severity { Note, Warning, Error };

/// @brief Secondary diagnostic note attached to a primary diagnostic.
struct DiagnosticNote {
    /// @brief Optional location associated with the note.
    SourceLoc loc{};

    /// @brief Human-readable explanatory text.
    std::string message;
};

/// @brief Machine-readable source replacement hint attached to a diagnostic.
struct DiagnosticFixIt {
    /// @brief Source range to replace. Empty/invalid means insert at the diagnostic location.
    SourceRange range{};

    /// @brief Replacement text.
    std::string replacement;

    /// @brief Human-readable description of the fix.
    std::string message;
};

/// @brief Single diagnostic message with location.
struct Diagnostic {
    Severity severity = Severity::Error; ///< Message severity
    std::string message;                 ///< Human-readable text
    SourceLoc loc;                       ///< Optional source location
    std::string code;                    ///< Optional diagnostic code (e.g., "B1001", "IL001")
    SourceRange range{};                 ///< Optional range to underline when printing snippets.
    std::vector<DiagnosticNote> notes{}; ///< Optional related notes.
    std::string stage{}; ///< Optional pipeline stage (parse, sema, lower, verify, runtime).
    std::string help{};  ///< Optional help text or URL for this diagnostic code.
    std::vector<DiagnosticFixIt> fixits{}; ///< Optional machine-readable fix suggestions.
};

/// @brief Collects diagnostics and prints them in order.
class DiagnosticEngine {
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

    /// @brief Access collected diagnostics for inspection.
    /// @return Const reference to the internal diagnostics vector.
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const {
        return diags_;
    }

  private:
    std::vector<Diagnostic> diags_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
};
} // namespace il::support
