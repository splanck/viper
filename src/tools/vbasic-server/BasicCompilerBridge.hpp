//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/vbasic-server/BasicCompilerBridge.hpp
// Purpose: Protocol-agnostic facade wrapping BASIC compiler APIs for the server.
// Key invariants:
//   - All methods are thread-safe (each creates fresh SourceManager/DiagnosticEngine)
//   - CompletionEngine is long-lived for LRU cache benefits
//   - Results are returned as simple structs via ICompilerBridge
// Ownership/Lifetime:
//   - BasicCompilerBridge owns the BasicCompletionEngine
//   - All returned data is fully owned (no dangling pointers)
// Links: tools/lsp-common/ICompilerBridge.hpp, frontends/basic/BasicAnalysis.hpp,
//        frontends/basic/BasicCompletion.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/lsp-common/ICompilerBridge.hpp"

#include <memory>

namespace il::frontends::basic {
class BasicCompletionEngine;
}

namespace viper::server {

/// @brief Protocol-agnostic facade wrapping BASIC compiler APIs.
///
/// Each method creates a fresh SourceManager per call for isolation.
/// The BasicCompletionEngine is shared across calls for LRU cache benefits.
class BasicCompilerBridge : public ICompilerBridge {
  public:
    BasicCompilerBridge();
    ~BasicCompilerBridge() override;

    // ── Analysis ──
    std::vector<DiagnosticInfo> check(const std::string &source, const std::string &path) override;
    CompileResult compile(const std::string &source, const std::string &path) override;

    // ── IDE Features ──
    std::vector<CompletionInfo> completions(const std::string &source,
                                            int line,
                                            int col,
                                            const std::string &path) override;
    std::string hover(const std::string &source,
                      int line,
                      int col,
                      const std::string &path) override;
    std::vector<SymbolInfo> symbols(const std::string &source, const std::string &path) override;

    // ── Dump ──
    std::string dumpIL(const std::string &source, const std::string &path, bool optimized) override;
    std::string dumpAst(const std::string &source, const std::string &path) override;
    std::string dumpTokens(const std::string &source, const std::string &path) override;

  private:
    std::unique_ptr<il::frontends::basic::BasicCompletionEngine> completionEngine_;
};

} // namespace viper::server
