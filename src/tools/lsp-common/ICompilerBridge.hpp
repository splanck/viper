//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/ICompilerBridge.hpp
// Purpose: Abstract interface for language server compiler bridges and
//          configuration for parameterizing shared handlers.
// Key invariants:
//   - All methods are pure virtual except runtime queries (shared default impl)
//   - ServerConfig parameterizes handler strings (names, prefixes, extensions)
// Ownership/Lifetime:
//   - Interface only; implementations own their compiler resources
// Links: tools/lsp-common/ServerTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/lsp-common/ServerTypes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace viper::server {

/// @brief Configuration for parameterizing shared LSP/MCP handlers.
struct ServerConfig {
    std::string serverName; ///< "zia-server" or "vbasic-server"
    std::string version;    ///< "0.1.0"
    std::string sourceName; ///< "zia" or "vbasic" (LSP diagnostic source)
    std::string toolPrefix; ///< "zia" or "basic" (MCP tool name prefix)
    std::string defaultExt; ///< ".zia" or ".bas"
    std::string langLabel;  ///< "Zia" or "Viper BASIC" (for tool descriptions)
};

/// @brief Abstract interface for protocol-agnostic compiler facades.
///
/// Both Zia and BASIC language servers implement this interface to provide
/// compilation, IDE features, and runtime queries through the shared
/// LSP and MCP handlers.
class ICompilerBridge {
  public:
    virtual ~ICompilerBridge() = default;

    // ── Analysis ──

    /// @brief Type-check source, return diagnostics (no codegen).
    virtual std::vector<DiagnosticInfo> check(const std::string &source,
                                              const std::string &path) = 0;

    /// @brief Full compilation, return success + diagnostics.
    virtual CompileResult compile(const std::string &source, const std::string &path) = 0;

    // ── IDE Features ──

    /// @brief Notify the bridge that a document is open/current in the editor.
    virtual void updateDocument(const std::string &path, const std::string &source);

    /// @brief Notify the bridge that a document has closed.
    virtual void removeDocument(const std::string &path);

    /// @brief Get completions at (line, col) in source.
    virtual std::vector<CompletionInfo> completions(const std::string &source,
                                                    int line,
                                                    int col,
                                                    const std::string &path) = 0;

    /// @brief Get type info for the symbol at (line, col).
    virtual std::string hover(const std::string &source,
                              int line,
                              int col,
                              const std::string &path) = 0;

    /// @brief List all top-level declarations in source.
    virtual std::vector<SymbolInfo> symbols(const std::string &source, const std::string &path) = 0;

    /// @brief Whether the bridge can answer go-to-definition requests.
    virtual bool supportsDefinition() const;

    /// @brief Whether the bridge can answer references requests.
    virtual bool supportsReferences() const;

    /// @brief Whether the bridge can compute semantic rename edits.
    virtual bool supportsRename() const;

    /// @brief Whether the bridge can answer signature-help requests.
    virtual bool supportsSignatureHelp() const;

    /// @brief Whether the bridge can list workspace symbols.
    virtual bool supportsWorkspaceSymbols() const;

    /// @brief Whether the bridge can provide semantic tokens.
    virtual bool supportsSemanticTokens() const;

    /// @brief Resolve the definition for the symbol at (line, col).
    virtual std::optional<LocationInfo> definition(const std::string &source,
                                                   int line,
                                                   int col,
                                                   const std::string &path);

    /// @brief Resolve all known references for the symbol at (line, col).
    virtual std::vector<LocationInfo> references(const std::string &source,
                                                 int line,
                                                 int col,
                                                 const std::string &path);

    /// @brief Compute workspace edits for renaming the symbol at (line, col).
    virtual RenameResult rename(const std::string &source,
                                int line,
                                int col,
                                const std::string &path,
                                const std::string &newName);

    /// @brief Return signature help for the active call at (line, col).
    virtual SignatureHelpInfo signatureHelp(const std::string &source,
                                            int line,
                                            int col,
                                            const std::string &path);

    /// @brief Search symbols across indexed/open workspace documents.
    virtual std::vector<SymbolInfo> workspaceSymbols(const std::string &query);

    /// @brief Return semantic tokens for the supplied source.
    virtual std::vector<SemanticTokenInfo> semanticTokens(const std::string &source,
                                                          const std::string &path);

    // ── Dump ──

    /// @brief Dump IL for source. If optimized, applies O1 optimization.
    virtual std::string dumpIL(const std::string &source,
                               const std::string &path,
                               bool optimized) = 0;

    /// @brief Dump AST for source.
    virtual std::string dumpAst(const std::string &source, const std::string &path) = 0;

    /// @brief Dump token stream for source.
    virtual std::string dumpTokens(const std::string &source, const std::string &path) = 0;

    // ── Runtime queries (shared default implementations) ──

    /// @brief List all runtime classes with member counts.
    virtual std::vector<RuntimeClassSummary> runtimeClasses();

    /// @brief List methods and properties for a runtime class.
    virtual std::vector<RuntimeMemberInfo> runtimeMembers(const std::string &className);

    /// @brief Search runtime APIs by keyword (case-insensitive substring match).
    virtual std::vector<RuntimeMemberInfo> runtimeSearch(const std::string &keyword);
};

} // namespace viper::server
