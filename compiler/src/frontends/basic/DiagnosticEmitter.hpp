//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the DiagnosticEmitter class, which formats and reports
// BASIC frontend diagnostics with rich context and source location information.
//
// The DiagnosticEmitter provides user-friendly error reporting throughout the
// BASIC compilation pipeline, transforming raw diagnostic messages into
// formatted output with:
// - Source file location (filename, line number, column)
// - Error codes for programmatic error handling
// - Source line context with caret (^) highlighting
// - Severity levels (error, warning, note)
// - Diagnostic message text
//
// Output Format:
//   program.bas:10:5: error: undefined variable 'counter' [E1001]
//   FOR counter = 1 TO 10
//       ^
//
// Key Responsibilities:
// - Diagnostic formatting: Converts internal diagnostic representations into
//   human-readable messages with source context
// - Source line extraction: Retrieves the relevant source line for each
//   diagnostic location to show the error in context
// - Caret positioning: Computes column offsets to place the ^ marker under
//   the problematic token or expression
// - Diagnostic ordering: Maintains emission order for stable, predictable
//   output across compilation runs
// - Source caching: Stores source text per file ID to enable efficient
//   repeated line lookups during diagnostic reporting
//
// Integration:
// - Used by: Lexer, Parser, SemanticAnalyzer, Lowerer to report errors
// - Wraps: DiagnosticEngine for diagnostic collection and counting
// - Queries: SourceManager for file paths and locations
// - Outputs to: std::ostream (typically std::cerr for error messages)
//
// Design Notes:
// - Borrows DiagnosticEngine and SourceManager; does not own them
// - Caches source text per file ID to avoid repeated file I/O
// - Diagnostics are accumulated and can be emitted in batch or individually
// - Thread-safe for diagnostic emission (though not typically used concurrently)
//
// Usage:
//   DiagnosticEmitter emitter(diagnosticEngine, sourceManager);
//   emitter.registerSource(fileId, sourceText);
//   // During compilation:
//   emitter.error(location, "Undefined variable", "E1001");
//   // After compilation:
//   emitter.flush(std::cerr);
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/Token.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::frontends::basic
{

/// @brief Formats BASIC diagnostics with error codes and caret ranges.
/// @invariant Diagnostics are emitted in order and printed with original source line.
/// @ownership Borrows DiagnosticEngine and SourceManager; copies source text per file id.
class DiagnosticEmitter
{
  public:
    /// @brief Create emitter forwarding counts to @p de and using @p sm for file paths.
    /// @param de Diagnostic engine collecting counts.
    /// @param sm Source manager providing file paths.
    DiagnosticEmitter(il::support::DiagnosticEngine &de, const il::support::SourceManager &sm);

    /// @brief Register source text for a file id.
    /// @param fileId Identifier from SourceManager.
    /// @param source Full contents of the source file.
    void addSource(uint32_t fileId, std::string source);

    /// @brief Emit diagnostic with @p code at @p loc covering @p length characters.
    /// @param sev Severity level.
    /// @param code BASIC error code (e.g., B1001).
    /// @param loc Start location of the diagnostic.
    /// @param length Number of characters to underline (0 -> 1 caret).
    /// @param message Human-readable explanation.
    void emit(il::support::Severity sev,
              std::string code,
              il::support::SourceLoc loc,
              uint32_t length,
              std::string message);

    /// @brief Emit standardized "expected vs got" parse diagnostic.
    /// @param got Actual token encountered.
    /// @param expect Expected token kind.
    /// @param loc Source location of the unexpected token.
    void emitExpected(TokenKind got, TokenKind expect, il::support::SourceLoc loc);

    /// @brief Print all diagnostics to @p os with source snippets.
    /// @param os Output stream.
    void printAll(std::ostream &os) const;

    /// @brief Number of errors reported.
    size_t errorCount() const;

    /// @brief Number of warnings reported.
    size_t warningCount() const;

    /// @brief Format a file:line string for a SourceLoc using SourceManager paths.
    /// @details Returns "<path>:<line>" when file and line are available, otherwise
    ///          returns an empty string.
    /// @param loc Source location to format.
    /// @return String with path and line or empty string when unavailable.
    std::string formatFileLine(il::support::SourceLoc loc) const;

  private:
    /// @brief Diagnostic record captured for later printing.
    struct Entry
    {
        il::support::Severity severity; ///< Diagnostic severity.
        std::string code;               ///< Error code like B1001.
        std::string message;            ///< Description text.
        il::support::SourceLoc loc;     ///< Start source location.
        uint32_t length;                ///< Number of characters to mark.
    };

    /// @brief Retrieve full line text for @p fileId at @p line.
    /// @param fileId Identifier from SourceManager.
    /// @param line 1-based line number to fetch.
    /// @return Line contents without trailing newline; empty if unavailable.
    std::string getLine(uint32_t fileId, uint32_t line) const;

    il::support::DiagnosticEngine &de_;                 ///< Underlying diagnostic engine.
    const il::support::SourceManager &sm_;              ///< Source manager for file paths.
    std::vector<Entry> entries_;                        ///< Diagnostics in emission order.
    std::unordered_map<uint32_t, std::string> sources_; ///< Source text per file id.
};

} // namespace il::frontends::basic
