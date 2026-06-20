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
    std::string file;     ///< Source file path for workspace-wide symbol results; may be empty.
};

/// @brief Source range in a compiler-owned file.
/// @details Coordinates are 1-based byte columns and half-open at the end.
struct SourceRangeInfo {
    std::string file;       ///< Source file path.
    uint32_t line{0};       ///< 1-based start line.
    uint32_t column{0};     ///< 1-based start column.
    uint32_t endLine{0};    ///< 1-based end line.
    uint32_t endColumn{0};  ///< 1-based end column, exclusive.
};

/// @brief Location result for definition/reference navigation.
struct LocationInfo {
    SourceRangeInfo range;  ///< File and range for the target occurrence.
    std::string name;       ///< Display name for the symbol.
    std::string kind;       ///< Symbol kind string.
    bool isDefinition{false};
};

/// @brief Single text edit produced by a workspace-level refactoring.
struct TextEditInfo {
    SourceRangeInfo range;  ///< File and range to replace.
    std::string newText;    ///< Replacement text.
};

/// @brief Result of a semantic rename request.
struct RenameResult {
    bool success{false};
    std::string reason;              ///< Empty when success is true.
    std::vector<TextEditInfo> edits; ///< Replacement edits grouped by file at the LSP boundary.
};

/// @brief Parameter metadata for signature help.
struct SignatureParameterInfo {
    std::string label;         ///< Full parameter label, usually "name: Type".
    std::string documentation; ///< Optional parameter documentation.
};

/// @brief One callable signature shown in signature help.
struct SignatureInfo {
    std::string label;         ///< Full signature label.
    std::string documentation; ///< Optional signature documentation.
    std::vector<SignatureParameterInfo> parameters;
};

/// @brief Signature help payload for the active call expression.
struct SignatureHelpInfo {
    bool available{false};
    int activeSignature{0};
    int activeParameter{0};
    std::vector<SignatureInfo> signatures;
};

/// @brief Semantic token type indices shared by the bridge and LSP legend.
enum class SemanticTokenType : uint32_t {
    Namespace = 0,
    Type = 1,
    Class = 2,
    Enum = 3,
    Interface = 4,
    Function = 5,
    Method = 6,
    Variable = 7,
    Parameter = 8,
    Property = 9,
    Keyword = 10,
    Number = 11,
    String = 12,
    Operator = 13,
};

/// @brief Semantic token emitted by a compiler bridge.
/// @details Coordinates are 1-based byte columns; LspHandler converts them to
///          zero-based UTF-16 semantic-token deltas.
struct SemanticTokenInfo {
    uint32_t line{0};
    uint32_t column{0};
    uint32_t length{0};
    SemanticTokenType type{SemanticTokenType::Variable};
    uint32_t modifiers{0};
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
