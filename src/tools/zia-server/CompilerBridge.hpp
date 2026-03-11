//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/zia-server/CompilerBridge.hpp
// Purpose: Protocol-agnostic facade wrapping Zia compiler APIs for the server.
// Key invariants:
//   - All methods are thread-safe (each creates fresh SourceManager/DiagnosticEngine)
//   - CompletionEngine is long-lived for LRU cache benefits
//   - Results are returned as simple structs, not compiler-internal types
// Ownership/Lifetime:
//   - CompilerBridge owns the CompletionEngine
//   - All returned data is fully owned (no dangling pointers)
// Links: frontends/zia/ZiaAnalysis.hpp, frontends/zia/ZiaCompletion.hpp,
//        frontends/zia/Compiler.hpp, il/runtime/classes/RuntimeClasses.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace il::frontends::zia
{
class CompletionEngine;
}

namespace viper::server
{

/// @brief Structured diagnostic from the compiler.
struct DiagnosticInfo
{
    int severity; ///< 0 = note, 1 = warning, 2 = error
    std::string message;
    std::string file;
    uint32_t line;
    uint32_t column;
    std::string code; ///< Warning/error code (e.g., "W001")
};

/// @brief Symbol information from semantic analysis.
struct SymbolInfo
{
    std::string name;
    std::string kind; ///< "variable", "parameter", "function", "method", "field", "type", "module"
    std::string type; ///< Type as string (e.g., "Integer", "List[String]")
    bool isFinal;
    bool isExtern;
};

/// @brief Completion item from the completion engine.
struct CompletionInfo
{
    std::string label;
    std::string insertText;
    int kind; ///< Maps to CompletionKind int (0-12)
    std::string detail;
    int sortPriority;
};

/// @brief Runtime class summary.
struct RuntimeClassSummary
{
    std::string qname;
    int propertyCount;
    int methodCount;
};

/// @brief Runtime member (method or property).
struct RuntimeMemberInfo
{
    std::string name;
    std::string memberKind; ///< "method" or "property"
    std::string signature;  ///< Method signature or property type
};

/// @brief Full compilation result.
struct CompileResult
{
    bool succeeded;
    std::vector<DiagnosticInfo> diagnostics;
};

/// @brief Protocol-agnostic facade wrapping Zia compiler APIs.
///
/// Each method creates a fresh SourceManager per call for isolation.
/// The CompletionEngine is shared across calls to benefit from its LRU cache.
class CompilerBridge
{
  public:
    CompilerBridge();
    ~CompilerBridge();

    // ── Analysis ──

    /// @brief Type-check source, return diagnostics (no codegen).
    std::vector<DiagnosticInfo> check(const std::string &source, const std::string &path);

    /// @brief Full compilation, return success + diagnostics.
    CompileResult compile(const std::string &source, const std::string &path);

    // ── IDE Features ──

    /// @brief Get completions at (line, col) in source.
    std::vector<CompletionInfo> completions(const std::string &source,
                                            int line,
                                            int col,
                                            const std::string &path);

    /// @brief Get type info for the symbol at (line, col).
    std::string hover(const std::string &source, int line, int col, const std::string &path);

    /// @brief List all top-level declarations in source.
    std::vector<SymbolInfo> symbols(const std::string &source, const std::string &path);

    // ── Dump ──

    /// @brief Dump IL for source. If optimized, applies O1 optimization.
    std::string dumpIL(const std::string &source, const std::string &path, bool optimized);

    /// @brief Dump AST for source.
    std::string dumpAst(const std::string &source, const std::string &path);

    /// @brief Dump token stream for source.
    std::string dumpTokens(const std::string &source, const std::string &path);

    // ── Runtime queries ──

    /// @brief List all runtime classes with member counts.
    std::vector<RuntimeClassSummary> runtimeClasses();

    /// @brief List methods and properties for a runtime class.
    std::vector<RuntimeMemberInfo> runtimeMembers(const std::string &className);

    /// @brief Search runtime APIs by keyword (case-insensitive substring match).
    std::vector<RuntimeMemberInfo> runtimeSearch(const std::string &keyword);

  private:
    std::unique_ptr<il::frontends::zia::CompletionEngine> completionEngine_;
};

} // namespace viper::server
