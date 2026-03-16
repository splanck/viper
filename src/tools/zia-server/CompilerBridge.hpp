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

namespace il::frontends::zia
{
class CompletionEngine;
}

namespace viper::server
{

/// @brief Protocol-agnostic facade wrapping Zia compiler APIs.
///
/// Each method creates a fresh SourceManager per call for isolation.
/// The CompletionEngine is shared across calls to benefit from its LRU cache.
class CompilerBridge : public ICompilerBridge
{
  public:
    CompilerBridge();
    ~CompilerBridge() override;

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
    std::unique_ptr<il::frontends::zia::CompletionEngine> completionEngine_;
};

} // namespace viper::server
