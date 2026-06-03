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

/// @brief Structured diagnostic from a compiler frontend.
struct DiagnosticInfo {
    int severity;        ///< 0 = note, 1 = warning, 2 = error
    std::string message; ///< Human-readable diagnostic text.
    std::string file;    ///< Source file the diagnostic refers to.
    uint32_t line;       ///< 1-based line number.
    uint32_t column;     ///< 1-based column number.
    std::string code;    ///< Warning/error code (e.g., "W001", "B1001")
};

/// @brief Symbol information from semantic analysis.
struct SymbolInfo {
    std::string name; ///< Symbol identifier.
    std::string kind; ///< "variable", "parameter", "function", "method", "field", "type", "module"
    std::string type; ///< Type as string (e.g., "Integer", "List[String]")
    bool isFinal;     ///< True if the symbol is immutable/final.
    bool isExtern;    ///< True if the symbol is externally declared.
};

/// @brief Completion item from a completion engine.
struct CompletionInfo {
    std::string label;      ///< Text shown in the completion list.
    std::string insertText; ///< Text inserted when the item is accepted.
    int kind;               ///< Maps to CompletionKind int (0-12)
    std::string detail;     ///< Secondary detail (e.g., type/signature).
    int sortPriority;       ///< Lower values sort earlier in the list.
};

/// @brief Runtime class summary.
struct RuntimeClassSummary {
    std::string qname; ///< Fully-qualified class name.
    int propertyCount; ///< Number of properties on the class.
    int methodCount;   ///< Number of methods on the class.
};

/// @brief Runtime member (method or property).
struct RuntimeMemberInfo {
    std::string name;       ///< Member name.
    std::string memberKind; ///< "method" or "property"
    std::string signature;  ///< Method signature or property type
};

/// @brief Full compilation result.
struct CompileResult {
    bool succeeded;                          ///< True when compilation produced no errors.
    std::vector<DiagnosticInfo> diagnostics; ///< Diagnostics emitted during compilation.
};

} // namespace viper::server
