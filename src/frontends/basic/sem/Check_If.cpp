//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/Check_If.cpp
// Purpose: Validate BASIC conditional statements and maintain semantic analyzer
//          invariants around scopes and control-flow context stacks.
// Key invariants:
//   * Every branch executes within its own scope to ensure variable lifetimes
//     mirror runtime behaviour.
//   * Conditions are validated using shared helpers so diagnostics align with
//     loop condition checks.
//   * Loop and label stacks maintained by @ref ControlCheckContext must remain
//     balanced regardless of branch structure.
// References: docs/basic-language.md#if-statements,
//             docs/codemap/basic.md#semantic-analyzer
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic checks for IF/ELSEIF/ELSE constructs.
/// @details Implements condition validation and scoped branch analysis while
///          keeping loop/label stacks balanced via ControlCheckContext.
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/Check_Common.hpp"

#include "frontends/basic/BasicDiagnosticMessages.hpp"

#include <string>

namespace il::frontends::basic::sem
{
namespace
{
/// @brief Analyse a branch arm (THEN/ELSEIF/ELSE) while maintaining scope.
///
/// The helper opens a new lexical scope for the branch and recursively visits
/// each statement.  When the branch is a statement list it iterates through the
/// children, otherwise it dispatches directly to @ref ControlCheckContext to
/// analyse the singular statement.  Null pointers are ignored, allowing earlier
/// parser errors to surface without cascading diagnostics.
///
/// @param context Control-flow checking context that tracks scopes and loops.
/// @param branch Pointer to the branch AST node (may be null or a statement list).
void analyzeBranch(ControlCheckContext &context, const StmtPtr &branch)
{
    if (!branch)
        return;

    auto scope = context.pushScope();
    if (const auto *list = dynamic_cast<const StmtList *>(branch.get()))
    {
        for (const auto &child : list->stmts)
        {
            if (!child)
                continue;
            context.visitStmt(*child);
        }
        return;
    }

    context.visitStmt(*branch);
}
} // namespace

/// @brief Validate a conditional expression used by IF/ELSEIF.
///
/// The helper evaluates the expression to recover its semantic type.  Boolean
/// results are accepted; all other types trigger diagnostic
/// `DiagNonBooleanCondition`, including a formatted representation of the source
/// expression when available.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param expr Expression node forming the condition.
void checkConditionExpr(SemanticAnalyzer &analyzer, Expr &expr)
{
    ControlCheckContext context(analyzer);
    const auto condTy = context.evaluateExpr(expr);
    using Type = SemanticAnalyzer::Type;

    if (condTy == Type::Unknown || condTy == Type::Bool)
        return;

    std::string exprText = semantic_analyzer_detail::conditionExprText(expr);
    if (exprText.empty())
        exprText = "<expr>";

    context.diagnostics().emitNonBooleanCondition(
        std::string(SemanticAnalyzer::DiagNonBooleanCondition),
        expr.loc,
        1,
        semantic_analyzer_detail::semanticTypeName(condTy),
        exprText);
}

/// @brief Analyse an IF statement, including optional ELSEIF/ELSE branches.
///
/// The helper validates the primary condition, then iteratively processes each
/// branch using @ref analyzeBranch so scopes and control-flow stacks remain
/// consistent.  ELSEIF arms validate their condition before visiting the branch,
/// mirroring the semantics of nested IF statements but sharing context state for
/// efficiency.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param stmt IF statement AST node to analyse.
void analyzeIf(SemanticAnalyzer &analyzer, const IfStmt &stmt)
{
    ControlCheckContext context(analyzer);

    if (stmt.cond)
        checkConditionExpr(context.analyzer(), *stmt.cond);

    analyzeBranch(context, stmt.then_branch);

    for (const auto &elseifArm : stmt.elseifs)
    {
        if (elseifArm.cond)
            checkConditionExpr(context.analyzer(), *elseifArm.cond);
        analyzeBranch(context, elseifArm.then_branch);
    }

    analyzeBranch(context, stmt.else_branch);
}

} // namespace il::frontends::basic::sem
