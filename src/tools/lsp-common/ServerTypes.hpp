//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/ServerTypes.hpp
// Purpose: Shared data types for language server bridges (diagnostics, symbols,
//          completions, runtime queries).
// Key invariants:
//   - All types are simple value structs with no compiler-internal dependencies
//   - Usable by any language server bridge (Zia, BASIC, etc.)
// Ownership/Lifetime:
//   - All data is fully owned (no dangling pointers)
// Links: tools/lsp-common/ICompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace viper::server {

/// @brief Related location attached to a diagnostic (e.g., "previous definition is here").
struct DiagnosticNoteInfo {
    std::string message; ///< Human-readable note text.
    std::string file;    ///< Source file the note refers to; empty when unknown.
    uint32_t line{0};    ///< 1-based line number; 0 when unknown.
    uint32_t column{0};  ///< 1-based column number; 0 when unknown.
};

/// @brief Machine-applicable replacement attached to a diagnostic.
/// @details The range is half-open: [line:column, endLine:endColumn). When
///          endLine/endColumn are 0 the fix-it is an insertion at line:column.
struct DiagnosticFixItInfo {
    std::string message;     ///< Human-readable description of the fix.
    std::string replacement; ///< Replacement text for the range.
    uint32_t line{0};        ///< 1-based begin line.
    uint32_t column{0};      ///< 1-based begin column.
    uint32_t endLine{0};     ///< 1-based end line (exclusive); 0 for insertions.
    uint32_t endColumn{0};   ///< 1-based end column (exclusive); 0 for insertions.
};

/// @brief Structured diagnostic from a compiler frontend.
struct DiagnosticInfo {
    int severity{0};       ///< 0 = note, 1 = warning, 2 = error
    std::string message;   ///< Human-readable diagnostic text.
    std::string file;      ///< Source file the diagnostic refers to.
    uint32_t line{0};      ///< 1-based line number.
    uint32_t column{0};    ///< 1-based column number.
    std::string code;      ///< Warning/error code (e.g., "W001", "B1001")
    uint32_t endLine{0};   ///< 1-based end line of the underlined range; 0 when unknown.
    uint32_t endColumn{0}; ///< 1-based end column (exclusive); 0 when unknown.
    std::string stage;     ///< Pipeline stage (parse, sema, lower, verify, runtime); may be empty.
    std::string help;      ///< Help text or URL for this diagnostic code; may be empty.
    std::vector<DiagnosticNoteInfo> notes;   ///< Related locations, in engine order.
    std::vector<DiagnosticFixItInfo> fixits; ///< Machine-applicable fixes, in engine order.
};

/// @brief Symbol information from semantic analysis.
struct SymbolInfo {
    std::string name; ///< Symbol identifier.
    std::string kind; ///< "variable", "parameter", "function", "method", "field", "type", "module"
    std::string type; ///< Type as string (e.g., "Integer", "List[String]")
    bool isFinal{false};  ///< True if the symbol is immutable/final.
    bool isExtern{false}; ///< True if the symbol is externally declared.
    uint32_t line{0};     ///< 1-based source line for the symbol declaration, or 0 if unknown.
    uint32_t column{0};   ///< 1-based source column for the symbol declaration, or 0 if unknown.
};

/// @brief Completion item from a completion engine.
struct CompletionInfo {
    std::string label;      ///< Text shown in the completion list.
    std::string insertText; ///< Text inserted when the item is accepted.
    int kind{0};            ///< Maps to CompletionKind int (0-12)
    std::string detail;     ///< Secondary detail (e.g., type/signature).
    int sortPriority{0};    ///< Lower values sort earlier in the list.
};

/// @brief Runtime class summary.
struct RuntimeClassSummary {
    std::string qname;    ///< Fully-qualified class name.
    int propertyCount{0}; ///< Number of properties on the class.
    int methodCount{0};   ///< Number of methods on the class.
};

/// @brief Runtime member (method or property).
struct RuntimeMemberInfo {
    std::string name;       ///< Member name.
    std::string memberKind; ///< "method" or "property"
    std::string signature;  ///< Method signature or property type
};

/// @brief Full compilation result.
struct CompileResult {
    bool succeeded{false};                   ///< True when compilation produced no errors.
    std::vector<DiagnosticInfo> diagnostics; ///< Diagnostics emitted during compilation.
};

} // namespace viper::server
