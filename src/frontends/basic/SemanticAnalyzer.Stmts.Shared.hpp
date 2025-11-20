// File: src/frontends/basic/SemanticAnalyzer.Stmts.Shared.hpp
// License: GPL-3.0-only. See LICENSE in the project root for full license
//          information.
// Purpose: Declares shared helpers for statement semantic analysis, providing
//          RAII utilities and mutation checks reused across themed statement
//          translation units.
// Key invariants: Helpers operate on an existing SemanticAnalyzer instance and
//                 never outlive it; loop/variable stacks remain balanced.
// Ownership/Lifetime: Helpers hold non-owning references to the analyzer.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/SemanticAnalyzer.hpp"

#include <string>
#include <string_view>

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Shared utilities reused by themed statement analyzers.
class StmtShared
{
  public:
    /// @brief Create helper bound to @p analyzer.
    explicit StmtShared(SemanticAnalyzer &analyzer) noexcept;

    StmtShared(const StmtShared &) = delete;
    StmtShared &operator=(const StmtShared &) = delete;

    /// @brief Guard that pushes a loop kind on construction and pops it on
    ///        destruction.
    class LoopGuard
    {
      public:
        LoopGuard(SemanticAnalyzer &analyzer, SemanticAnalyzer::LoopKind kind) noexcept;
        LoopGuard(const LoopGuard &) = delete;
        LoopGuard &operator=(const LoopGuard &) = delete;
        LoopGuard(LoopGuard &&other) noexcept;
        LoopGuard &operator=(LoopGuard &&other) noexcept;
        ~LoopGuard() noexcept;

      private:
        SemanticAnalyzer *analyzer_{nullptr};
    };

    /// @brief Guard that records an active FOR loop variable for the current
    ///        statement body.
    class ForLoopGuard
    {
      public:
        ForLoopGuard(SemanticAnalyzer &analyzer, std::string variable);
        ForLoopGuard(const ForLoopGuard &) = delete;
        ForLoopGuard &operator=(const ForLoopGuard &) = delete;
        ForLoopGuard(ForLoopGuard &&other) noexcept;
        ForLoopGuard &operator=(ForLoopGuard &&other) noexcept;
        ~ForLoopGuard() noexcept;

      private:
        SemanticAnalyzer *analyzer_{nullptr};
    };

    /// @brief Determine whether @p name is currently an active FOR loop variable.
    [[nodiscard]] bool isLoopVariable(std::string_view name) const noexcept;

    /// @brief Emit the standard diagnostic for mutating a loop variable.
    void reportLoopVariableMutation(const std::string &name,
                                    const il::support::SourceLoc &loc,
                                    uint32_t width);

  protected:
    /// @brief Access bound analyzer.
    SemanticAnalyzer &analyzer() noexcept
    {
        return analyzer_;
    }

    /// @brief Access bound analyzer.
    const SemanticAnalyzer &analyzer() const noexcept
    {
        return analyzer_;
    }

  private:
    SemanticAnalyzer &analyzer_;
};

} // namespace il::frontends::basic::semantic_analyzer_detail
