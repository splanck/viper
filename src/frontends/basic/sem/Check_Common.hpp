//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Shared infrastructure for control-flow and expression semantic
///        checkers.
/// @details Provides thin context wrappers and helper routines that expose the
///          mutable state used by control-statement analyzers (loop stacks and
///          label tracking) and expression analyzers (type queries and implicit
///          conversions) while asserting invariants when a checker completes.
///          Individual checkers live in dedicated translation units.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/SemanticAnalyzer_Internal.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace il::frontends::basic::sem
{

/// @brief Shared context for control-statement semantic checks.
/// @details Wraps the analyzer state so helpers can manipulate loop and label
///          tracking consistently. On destruction the context asserts that loop
///          and FOR-variable stacks have been balanced by the checker.
class ControlCheckContext
{
  public:
    explicit ControlCheckContext(SemanticAnalyzer &analyzer) noexcept
        : analyzer_(&analyzer), stmtContext_(analyzer), loopDepth_(analyzer.loopStack_.size()),
          forDepth_(analyzer.forStack_.size())
    {
    }

    ControlCheckContext(const ControlCheckContext &) = delete;
    ControlCheckContext &operator=(const ControlCheckContext &) = delete;

    ~ControlCheckContext()
    {
        assert(analyzer_ != nullptr && "context detached from analyzer");
        assert(analyzer_->loopStack_.size() == loopDepth_ &&
               "loop stack unbalanced by control-flow check");
        assert(analyzer_->forStack_.size() == forDepth_ &&
               "FOR stack unbalanced by control-flow check");
    }

    [[nodiscard]] SemanticAnalyzer &analyzer() noexcept
    {
        return *analyzer_;
    }

    [[nodiscard]] const SemanticAnalyzer &analyzer() const noexcept
    {
        return *analyzer_;
    }

    [[nodiscard]] semantic_analyzer_detail::ControlStmtContext &stmt() noexcept
    {
        return stmtContext_;
    }

    [[nodiscard]] const semantic_analyzer_detail::ControlStmtContext &stmt() const noexcept
    {
        return stmtContext_;
    }

    [[nodiscard]] bool hasKnownLabel(int label) const noexcept
    {
        return analyzer_->labels_.count(label) != 0;
    }

    [[nodiscard]] bool hasReferencedLabel(int label) const noexcept
    {
        return analyzer_->labelRefs_.count(label) != 0;
    }

    bool insertLabelReference(int label)
    {
        auto insertResult = analyzer_->labelRefs_.insert(label);
        if (insertResult.second && analyzer_->activeProcScope_)
            analyzer_->activeProcScope_->noteLabelRefInserted(label);
        return insertResult.second;
    }

    [[nodiscard]] bool hasActiveLoop() const noexcept
    {
        return !analyzer_->loopStack_.empty();
    }

    [[nodiscard]] SemanticAnalyzer::LoopKind currentLoop() const noexcept
    {
        assert(hasActiveLoop() && "no active loop available");
        return analyzer_->loopStack_.back();
    }

    semantic_analyzer_detail::ControlStmtContext::LoopGuard whileLoopGuard()
    {
        return {*analyzer_, SemanticAnalyzer::LoopKind::While};
    }

    semantic_analyzer_detail::ControlStmtContext::LoopGuard doLoopGuard()
    {
        return {*analyzer_, SemanticAnalyzer::LoopKind::Do};
    }

    semantic_analyzer_detail::ControlStmtContext::LoopGuard forLoopGuard()
    {
        return {*analyzer_, SemanticAnalyzer::LoopKind::For};
    }

    semantic_analyzer_detail::ControlStmtContext::LoopGuard subLoopGuard()
    {
        return {*analyzer_, SemanticAnalyzer::LoopKind::Sub};
    }

    semantic_analyzer_detail::ControlStmtContext::LoopGuard functionLoopGuard()
    {
        return {*analyzer_, SemanticAnalyzer::LoopKind::Function};
    }

    semantic_analyzer_detail::ControlStmtContext::ForLoopGuard trackForVariable(std::string name)
    {
        return {*analyzer_, std::move(name)};
    }

    SemanticAnalyzer::LoopKind toLoopKind(ExitStmt::LoopKind kind) const noexcept
    {
        switch (kind)
        {
            case ExitStmt::LoopKind::For:
                return SemanticAnalyzer::LoopKind::For;
            case ExitStmt::LoopKind::While:
                return SemanticAnalyzer::LoopKind::While;
            case ExitStmt::LoopKind::Do:
                return SemanticAnalyzer::LoopKind::Do;
            case ExitStmt::LoopKind::Sub:
                return SemanticAnalyzer::LoopKind::Sub;
            case ExitStmt::LoopKind::Function:
                return SemanticAnalyzer::LoopKind::Function;
        }
        return SemanticAnalyzer::LoopKind::While;
    }

    const char *loopKindName(SemanticAnalyzer::LoopKind kind) const noexcept
    {
        switch (kind)
        {
            case SemanticAnalyzer::LoopKind::For:
                return "FOR";
            case SemanticAnalyzer::LoopKind::While:
                return "WHILE";
            case SemanticAnalyzer::LoopKind::Do:
                return "DO";
            case SemanticAnalyzer::LoopKind::Sub:
                return "SUB";
            case SemanticAnalyzer::LoopKind::Function:
                return "FUNCTION";
        }
        return "WHILE";
    }

    ScopeTracker::ScopedScope pushScope()
    {
        return ScopeTracker::ScopedScope(analyzer_->scopes_);
    }

    [[nodiscard]] bool hasForVariable() const noexcept
    {
        return !analyzer_->forStack_.empty();
    }

    [[nodiscard]] std::string_view currentForVariable() const noexcept
    {
        if (analyzer_->forStack_.empty())
            return {};
        return analyzer_->forStack_.back();
    }

    void popForVariable()
    {
        analyzer_->popForVariable();
    }

    void installErrorHandler(int label)
    {
        analyzer_->installErrorHandler(label);
    }

    void clearErrorHandler()
    {
        analyzer_->clearErrorHandler();
    }

    [[nodiscard]] bool hasActiveErrorHandler() const noexcept
    {
        return analyzer_->hasActiveErrorHandler();
    }

    [[nodiscard]] bool hasActiveProcScope() const noexcept
    {
        return analyzer_->activeProcScope_ != nullptr;
    }

    [[nodiscard]] bool hasLoopOfKind(SemanticAnalyzer::LoopKind kind) const noexcept
    {
        return analyzer_->hasLoopOfKind(kind);
    }

    [[nodiscard]] SemanticDiagnostics &diagnostics() noexcept
    {
        return analyzer_->de;
    }

    void resolveLoopVariable(std::string &name)
    {
        analyzer_->resolveAndTrackSymbol(name, SemanticAnalyzer::SymbolKind::Definition);
    }

    SemanticAnalyzer::Type evaluateExpr(Expr &expr)
    {
        return analyzer_->visitExpr(expr);
    }

    void visitStmt(Stmt &stmt)
    {
        analyzer_->visitStmt(stmt);
    }

    void markImplicitConversion(const Expr &expr, SemanticAnalyzer::Type target)
    {
        analyzer_->markImplicitConversion(expr, target);
    }

  private:
    SemanticAnalyzer *analyzer_{nullptr};
    semantic_analyzer_detail::ControlStmtContext stmtContext_;
    size_t loopDepth_ = 0;
    size_t forDepth_ = 0;
};

/// @brief Context wrapper for expression checkers.
/// @details Provides helpers for evaluating child expressions and recording
///          implicit conversions while exposing diagnostics. visitExpr expects
///          mutable nodes, so evaluate() internally casts away constness when
///          callers only have const handles.
class ExprCheckContext
{
  public:
    explicit ExprCheckContext(SemanticAnalyzer &analyzer) noexcept : analyzer_(&analyzer) {}

    [[nodiscard]] SemanticAnalyzer &analyzer() noexcept
    {
        return *analyzer_;
    }

    [[nodiscard]] const SemanticAnalyzer &analyzer() const noexcept
    {
        return *analyzer_;
    }

    SemanticAnalyzer::Type evaluate(Expr &expr)
    {
        return analyzer_->visitExpr(expr);
    }

    SemanticAnalyzer::Type evaluate(const Expr &expr)
    {
        return analyzer_->visitExpr(const_cast<Expr &>(expr));
    }

    void markImplicitConversion(const Expr &expr, SemanticAnalyzer::Type target)
    {
        analyzer_->markImplicitConversion(expr, target);
    }

    [[nodiscard]] SemanticDiagnostics &diagnostics() noexcept
    {
        return analyzer_->de;
    }

    const ProcSignature *resolveCallee(const CallExpr &expr, ProcSignature::Kind kind)
    {
        return analyzer_->resolveCallee(expr, kind);
    }

    std::vector<SemanticAnalyzer::Type> checkCallArgs(const CallExpr &expr,
                                                      const ProcSignature *sig)
    {
        return analyzer_->checkCallArgs(expr, sig);
    }

    SemanticAnalyzer::Type inferCallType(const CallExpr &expr, const ProcSignature *sig)
    {
        return analyzer_->inferCallType(expr, sig);
    }

    // Variable analysis helpers
    void resolveAndTrackSymbol(std::string &name, SemanticAnalyzer::SymbolKind kind)
    {
        analyzer_->resolveAndTrackSymbol(name, kind);
    }

    void resolveAndTrackSymbolRef(std::string &name)
    {
        analyzer_->resolveAndTrackSymbol(name, SemanticAnalyzer::SymbolKind::Reference);
    }

    [[nodiscard]] bool hasSymbol(const std::string &name) const
    {
        return analyzer_->symbols_.count(name) != 0;
    }

    [[nodiscard]] const std::unordered_set<std::string> &symbols() const noexcept
    {
        return analyzer_->symbols_;
    }

    [[nodiscard]] std::optional<SemanticAnalyzer::Type> varType(const std::string &name) const
    {
        auto it = analyzer_->varTypes_.find(name);
        if (it != analyzer_->varTypes_.end())
            return it->second;
        return std::nullopt;
    }

    // Array analysis helpers
    [[nodiscard]] bool hasArray(const std::string &name) const
    {
        return analyzer_->arrays_.count(name) != 0;
    }

    [[nodiscard]] const ArrayMetadata *arrayMetadata(const std::string &name) const
    {
        auto it = analyzer_->arrays_.find(name);
        if (it != analyzer_->arrays_.end())
            return &it->second;
        return nullptr;
    }

    void insertImplicitCast(Expr &expr, SemanticAnalyzer::Type target)
    {
        analyzer_->insertImplicitCast(expr, target);
    }

  private:
    SemanticAnalyzer *analyzer_{nullptr};
};

inline void emitTypeMismatch(SemanticDiagnostics &diagnostics,
                             std::string code,
                             il::support::SourceLoc loc,
                             uint32_t length,
                             std::string message)
{
    diagnostics.emit(
        il::support::Severity::Error, std::move(code), loc, length, std::move(message));
}

inline void emitOperandTypeMismatch(SemanticDiagnostics &diagnostics,
                                    const BinaryExpr &expr,
                                    std::string_view diagId)
{
    if (diagId.empty())
        return;

    emitTypeMismatch(diagnostics, std::string(diagId), expr.loc, 1, "operand type mismatch");
}

inline void emitDivideByZero(SemanticDiagnostics &diagnostics, const BinaryExpr &expr)
{
    diagnostics.emit(il::support::Severity::Error, "B2002", expr.loc, 1, "divide by zero");
}

// Per-construct dispatcher entry points implemented in dedicated translation
// units.
void checkConditionExpr(SemanticAnalyzer &analyzer, Expr &expr);
void analyzeIf(SemanticAnalyzer &analyzer, const IfStmt &stmt);
void analyzeSelectCase(SemanticAnalyzer &analyzer, const SelectCaseStmt &stmt);
void analyzeSelectCaseBody(SemanticAnalyzer &analyzer, const std::vector<StmtPtr> &body);
void analyzeWhile(SemanticAnalyzer &analyzer, const WhileStmt &stmt);
void analyzeDo(SemanticAnalyzer &analyzer, const DoStmt &stmt);
void analyzeFor(SemanticAnalyzer &analyzer, ForStmt &stmt);
void analyzeForEach(SemanticAnalyzer &analyzer, ForEachStmt &stmt);
void analyzeExit(SemanticAnalyzer &analyzer, const ExitStmt &stmt);
void analyzeGoto(SemanticAnalyzer &analyzer, const GotoStmt &stmt);
void analyzeGosub(SemanticAnalyzer &analyzer, const GosubStmt &stmt);
void analyzeOnErrorGoto(SemanticAnalyzer &analyzer, const OnErrorGoto &stmt);
void analyzeNext(SemanticAnalyzer &analyzer, const NextStmt &stmt);
void analyzeResume(SemanticAnalyzer &analyzer, const Resume &stmt);
void analyzeReturn(SemanticAnalyzer &analyzer, ReturnStmt &stmt);
SemanticAnalyzer::Type analyzeUnaryExpr(SemanticAnalyzer &analyzer, const UnaryExpr &expr);
SemanticAnalyzer::Type analyzeBinaryExpr(SemanticAnalyzer &analyzer, const BinaryExpr &expr);
SemanticAnalyzer::Type analyzeCallExpr(SemanticAnalyzer &analyzer, const CallExpr &expr);
SemanticAnalyzer::Type analyzeVarExpr(SemanticAnalyzer &analyzer, VarExpr &expr);
SemanticAnalyzer::Type analyzeArrayExpr(SemanticAnalyzer &analyzer, ArrayExpr &expr);
SemanticAnalyzer::Type analyzeLBoundExpr(SemanticAnalyzer &analyzer, LBoundExpr &expr);
SemanticAnalyzer::Type analyzeUBoundExpr(SemanticAnalyzer &analyzer, UBoundExpr &expr);

} // namespace il::frontends::basic::sem
