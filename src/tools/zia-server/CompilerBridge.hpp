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
//        frontends/zia/Compiler.hpp, tools/lsp-common/ICompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/lsp-common/ICompilerBridge.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace il::frontends::zia {
class CompletionEngine;
}

namespace viper::server {

/// @brief Protocol-agnostic facade wrapping Zia compiler APIs.
///
/// Each method creates a fresh SourceManager per call for isolation.
/// The CompletionEngine is shared across calls to benefit from its LRU cache.
class CompilerBridge : public ICompilerBridge {
  public:
    CompilerBridge();
    ~CompilerBridge() override;

    // ── Analysis ──

    std::vector<DiagnosticInfo> check(const std::string &source, const std::string &path) override;
    CompileResult compile(const std::string &source, const std::string &path) override;

    // ── IDE Features ──

    void updateDocument(const std::string &path, const std::string &source) override;
    void removeDocument(const std::string &path) override;

    std::vector<CompletionInfo> completions(const std::string &source,
                                            int line,
                                            int col,
                                            const std::string &path) override;
    std::string hover(const std::string &source,
                      int line,
                      int col,
                      const std::string &path) override;
    std::vector<SymbolInfo> symbols(const std::string &source, const std::string &path) override;

    bool supportsDefinition() const override;
    bool supportsReferences() const override;
    bool supportsRename() const override;
    bool supportsSignatureHelp() const override;
    bool supportsWorkspaceSymbols() const override;
    bool supportsSemanticTokens() const override;

    std::optional<LocationInfo> definition(const std::string &source,
                                           int line,
                                           int col,
                                           const std::string &path) override;
    std::vector<LocationInfo> references(const std::string &source,
                                         int line,
                                         int col,
                                         const std::string &path) override;
    RenameResult rename(const std::string &source,
                        int line,
                        int col,
                        const std::string &path,
                        const std::string &newName) override;
    SignatureHelpInfo signatureHelp(const std::string &source,
                                    int line,
                                    int col,
                                    const std::string &path) override;
    std::vector<SymbolInfo> workspaceSymbols(const std::string &query) override;
    std::vector<SemanticTokenInfo> semanticTokens(const std::string &source,
                                                  const std::string &path) override;

    // ── Dump ──

    std::string dumpIL(const std::string &source, const std::string &path, bool optimized) override;
    std::string dumpAst(const std::string &source, const std::string &path) override;
    std::string dumpTokens(const std::string &source, const std::string &path) override;

  private:
    /// @brief Guards access to the shared completion engine cache.
    /// @details Other bridge operations build fresh compiler state per call, but completions reuse
    /// a
    ///          long-lived engine for cache locality. The mutex preserves the documented
    ///          thread-safe bridge contract if the server dispatch model becomes concurrent.
    mutable std::mutex completionMutex_;
    std::unique_ptr<il::frontends::zia::CompletionEngine> completionEngine_;

    /// @brief Guards project index updates and workspace symbol scans.
    mutable std::mutex projectMutex_;
    void *projectIndex_{nullptr};
    /// @brief False after a runtime project-index mutation reports failure.
    /// @details Capability advertisement remains independent from this flag;
    ///          operations consult it to fail gracefully without hiding the
    ///          feature from clients that can recover after document changes.
    bool projectIndexUsable_{true};
    std::unordered_map<std::string, std::string> openDocuments_;
    /// @brief Cached workspace symbols derived from open documents and nearby files.
    std::vector<SymbolInfo> workspaceSymbolCache_;
    /// @brief True when open document changes require rebuilding workspace symbols.
    bool workspaceSymbolCacheDirty_{true};
};

} // namespace viper::server
