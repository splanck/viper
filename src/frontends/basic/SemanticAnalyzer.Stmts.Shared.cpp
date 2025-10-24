//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the shared helpers used by statement semantic analysis, including
// RAII guards for loop tracking and utility functions for type enforcement.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Shared semantic-analysis utilities for BASIC statements.
/// @details Provides loop tracking helpers, type assertions, and RAII guards
///          that ensure @ref SemanticAnalyzer invariants are maintained even when
///          control flow unwinds due to diagnostics.

#include "frontends/basic/SemanticAnalyzer.Stmts.Shared.hpp"

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <utility>

namespace il::frontends::basic
{

/// @brief Ensure an expression has a numeric type.
/// @details Evaluates @p expr and emits diagnostic B2001 when the result is not
///          numeric, appending @p message to explain the context.
/// @param expr Expression to validate.
/// @param message Prefix for the diagnostic message.
void SemanticAnalyzer::requireNumeric(Expr &expr, std::string_view message)
{
    Type exprType = visitExpr(expr);
    if (exprType == Type::Unknown || exprType == Type::Int || exprType == Type::Float)
        return;

    std::string msg(message);
    msg += ", got ";
    msg += semantic_analyzer_detail::semanticTypeName(exprType);
    msg += '.';

    de.emit(il::support::Severity::Error, "B2001", expr.loc, 1, std::move(msg));
}

/// @brief Record entry into a loop of the specified kind.
/// @details Pushes @p kind onto the loop stack so nested constructs can validate
///          statements like EXIT or NEXT.
/// @param kind Loop type being entered.
void SemanticAnalyzer::pushLoop(LoopKind kind)
{
    loopStack_.push_back(kind);
}

/// @brief Mark exit from the innermost loop.
/// @details Pops the loop stack if present, guarding against unbalanced calls.
void SemanticAnalyzer::popLoop()
{
    if (!loopStack_.empty())
        loopStack_.pop_back();
}

/// @brief Track a FOR-loop control variable by name.
/// @details Stores the variable so assignments can be flagged while the loop is
///          active.
/// @param name Name of the FOR-loop variable.
void SemanticAnalyzer::pushForVariable(std::string name)
{
    forStack_.push_back(std::move(name));
}

/// @brief Remove the most recently tracked FOR-loop variable.
/// @details Pops the stack if non-empty, mirroring loop exit.
void SemanticAnalyzer::popForVariable()
{
    if (!forStack_.empty())
        forStack_.pop_back();
}

/// @brief Test whether a variable is currently registered as a FOR-loop control variable.
/// @param name Identifier to search for.
/// @return True when @p name appears in the active FOR-loop stack.
bool SemanticAnalyzer::isLoopVariableActive(std::string_view name) const noexcept
{
    return std::find(forStack_.begin(), forStack_.end(), name) != forStack_.end();
}

} // namespace il::frontends::basic

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Construct shared helpers bound to the owning analyser.
/// @param analyzer Semantic analyser that maintains loop stacks and diagnostics.
StmtShared::StmtShared(SemanticAnalyzer &analyzer) noexcept : analyzer_(analyzer) {}

/// @brief RAII guard that records loop entry and exit.
/// @details Pushes @p kind onto the loop stack during construction and pops it
///          during destruction.
StmtShared::LoopGuard::LoopGuard(SemanticAnalyzer &analyzer,
                                 SemanticAnalyzer::LoopKind kind) noexcept
    : analyzer_(&analyzer)
{
    analyzer_->pushLoop(kind);
}

/// @brief Move-construct the guard, transferring ownership of the stack entry.
StmtShared::LoopGuard::LoopGuard(LoopGuard &&other) noexcept : analyzer_(other.analyzer_)
{
    other.analyzer_ = nullptr;
}

/// @brief Move-assign the guard, releasing any existing loop tracking.
StmtShared::LoopGuard &StmtShared::LoopGuard::operator=(LoopGuard &&other) noexcept
{
    if (this == &other)
        return *this;
    if (analyzer_)
        analyzer_->popLoop();
    analyzer_ = other.analyzer_;
    other.analyzer_ = nullptr;
    return *this;
}

/// @brief Pop the loop stack when the guard goes out of scope.
StmtShared::LoopGuard::~LoopGuard() noexcept
{
    if (analyzer_)
        analyzer_->popLoop();
}

/// @brief RAII guard that tracks FOR-loop variables.
/// @details Registers @p variable on construction and removes it when destroyed.
StmtShared::ForLoopGuard::ForLoopGuard(SemanticAnalyzer &analyzer, std::string variable)
    : analyzer_(&analyzer)
{
    analyzer_->pushForVariable(std::move(variable));
}

/// @brief Move-construct a guard, adopting the tracked variable.
StmtShared::ForLoopGuard::ForLoopGuard(ForLoopGuard &&other) noexcept : analyzer_(other.analyzer_)
{
    other.analyzer_ = nullptr;
}

/// @brief Move-assign a guard, releasing any currently tracked variable first.
StmtShared::ForLoopGuard &StmtShared::ForLoopGuard::operator=(ForLoopGuard &&other) noexcept
{
    if (this == &other)
        return *this;
    if (analyzer_)
        analyzer_->popForVariable();
    analyzer_ = other.analyzer_;
    other.analyzer_ = nullptr;
    return *this;
}

/// @brief Deregister the loop variable when the guard is destroyed.
StmtShared::ForLoopGuard::~ForLoopGuard() noexcept
{
    if (analyzer_)
        analyzer_->popForVariable();
}

/// @brief Determine whether a name refers to an active FOR-loop variable.
/// @param name Identifier to test.
/// @return True when @p name is currently tracked via @ref ForLoopGuard.
bool StmtShared::isLoopVariable(std::string_view name) const noexcept
{
    return analyzer_.isLoopVariableActive(name);
}

/// @brief Emit a diagnostic when a FOR-loop variable is mutated inside the loop body.
/// @param name Name of the loop variable.
/// @param loc Source location of the offending assignment.
/// @param width Highlight width for caret printing.
void StmtShared::reportLoopVariableMutation(const std::string &name,
                                            const il::support::SourceLoc &loc,
                                            uint32_t width)
{
    std::string msg = "cannot assign to loop variable '" + name + "' inside FOR";
    analyzer_.de.emit(il::support::Severity::Error, "B1010", loc, width, std::move(msg));
}

} // namespace il::frontends::basic::semantic_analyzer_detail
