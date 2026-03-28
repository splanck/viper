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
    int severity; ///< 0 = note, 1 = warning, 2 = error
    std::string message;
    std::string file;
    uint32_t line;
    uint32_t column;
    std::string code; ///< Warning/error code (e.g., "W001", "B1001")
};

/// @brief Symbol information from semantic analysis.
struct SymbolInfo {
    std::string name;
    std::string kind; ///< "variable", "parameter", "function", "method", "field", "type", "module"
    std::string type; ///< Type as string (e.g., "Integer", "List[String]")
    bool isFinal;
    bool isExtern;
};

/// @brief Completion item from a completion engine.
struct CompletionInfo {
    std::string label;
    std::string insertText;
    int kind; ///< Maps to CompletionKind int (0-12)
    std::string detail;
    int sortPriority;
};

/// @brief Runtime class summary.
struct RuntimeClassSummary {
    std::string qname;
    int propertyCount;
    int methodCount;
};

/// @brief Runtime member (method or property).
struct RuntimeMemberInfo {
    std::string name;
    std::string memberKind; ///< "method" or "property"
    std::string signature;  ///< Method signature or property type
};

/// @brief Full compilation result.
struct CompileResult {
    bool succeeded;
    std::vector<DiagnosticInfo> diagnostics;
};

} // namespace viper::server
