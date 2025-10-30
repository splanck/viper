//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/Check_Loops.cpp
// Purpose: Validate BASIC loop constructs and maintain the semantic analyzer's
//          loop/label bookkeeping while emitting targeted diagnostics.
// Key invariants:
//   * ControlCheckContext maintains loop stacks and scope guards to mirror the
//     runtime nesting structure; every helper must push/pop correctly.
//   * EXIT/NEXT statements verify they match an active loop, ensuring the
//     resulting control flow remains well-structured.
//   * Condition expressions are validated with shared helpers so diagnostics are
//     consistent across IF and loop constructs.
// References: docs/basic-language.md#loops,
//             docs/codemap/basic.md#semantic-analyzer
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic checks for loop constructs (WHILE, DO, FOR, NEXT, EXIT).
/// @details Applies condition validation, loop stack management, and loop
///          variable tracking using the shared control-flow context helpers.
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/Check_Common.hpp"

#include <string>

namespace il::frontends::basic::sem
{

/// @brief Validate a WHILE loop and analyse its body.
///
/// The helper ensures the loop condition is type-checked (if present) and then
/// visits each body statement within a new scope.  It acquires a loop guard from
/// @ref ControlCheckContext to record that a WHILE loop is active so nested EXIT
/// statements can target it accurately.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param stmt AST node representing the WHILE statement.
void analyzeWhile(SemanticAnalyzer &analyzer, const WhileStmt &stmt)
{
    ControlCheckContext context(analyzer);
    if (stmt.cond)
        checkConditionExpr(context.analyzer(), *stmt.cond);

    [[maybe_unused]] auto loopGuard = context.whileLoopGuard();
    [[maybe_unused]] auto scope = context.pushScope();
    for (const auto &bodyStmt : stmt.body)
    {
        if (!bodyStmt)
            continue;
        context.visitStmt(*bodyStmt);
    }
}

/// @brief Validate a DO[/LOOP] construct, handling both pre- and post-test forms.
///
/// The helper checks the condition when it appears, sets up loop/scope guards,
/// and walks the loop body.  When the condition occurs after the body the same
/// validation logic runs again to mirror BASIC's post-test semantics.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param stmt AST node representing the DO statement.
void analyzeDo(SemanticAnalyzer &analyzer, const DoStmt &stmt)
{
    ControlCheckContext context(analyzer);
    const auto checkCond = [&]()
    {
        if (stmt.cond)
            checkConditionExpr(context.analyzer(), *stmt.cond);
    };

    if (stmt.testPos == DoStmt::TestPos::Pre)
        checkCond();

    [[maybe_unused]] auto loopGuard = context.doLoopGuard();
    {
        [[maybe_unused]] auto scope = context.pushScope();
        for (const auto &bodyStmt : stmt.body)
        {
            if (!bodyStmt)
                continue;
            context.visitStmt(*bodyStmt);
        }
    }

    if (stmt.testPos == DoStmt::TestPos::Post)
        checkCond();
}

/// @brief Validate a FOR loop, including loop variable tracking and body analysis.
///
/// The helper resolves the loop variable, evaluates start/end/step expressions,
/// and records the active FOR variable so NEXT statements can be checked for
/// mismatches.  The loop guard scopes the loop on the analyzer's stack to ensure
/// EXIT statements recognise the context.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param stmt AST node representing the FOR statement (non-const because the
///             analyzer records implicit conversions on it).
void analyzeFor(SemanticAnalyzer &analyzer, ForStmt &stmt)
{
    ControlCheckContext context(analyzer);
    context.resolveLoopVariable(stmt.var);
    if (stmt.start)
        context.evaluateExpr(*stmt.start, stmt.start);
    if (stmt.end)
        context.evaluateExpr(*stmt.end, stmt.end);
    if (stmt.step)
        context.evaluateExpr(*stmt.step, stmt.step);

    [[maybe_unused]] auto forGuard = context.trackForVariable(stmt.var);
    [[maybe_unused]] auto loopGuard = context.forLoopGuard();
    [[maybe_unused]] auto scope = context.pushScope();
    for (const auto &bodyStmt : stmt.body)
    {
        if (!bodyStmt)
            continue;
        context.visitStmt(*bodyStmt);
    }
}

/// @brief Validate a NEXT statement, ensuring it matches an active FOR loop.
///
/// NEXT can optionally name the loop variable.  The helper verifies that a FOR
/// loop is active and, when a variable is provided, that it matches the innermost
/// loop.  Diagnostics B1002 surface when mismatches occur.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param stmt AST node representing the NEXT statement.
void analyzeNext(SemanticAnalyzer &analyzer, const NextStmt &stmt)
{
    ControlCheckContext context(analyzer);
    if (!context.hasForVariable() ||
        (!stmt.var.empty() && stmt.var != context.currentForVariable()))
    {
        std::string msg = "mismatched NEXT";
        if (!stmt.var.empty())
        {
            msg += " '";
            msg += stmt.var;
            msg += "'";
        }
        if (context.hasForVariable())
        {
            msg += ", expected '";
            msg += std::string(context.currentForVariable());
            msg += "'";
        }
        else
        {
            msg += ", no active FOR";
        }
        context.diagnostics().emit(
            il::support::Severity::Error, "B1002", stmt.loc, 4, std::move(msg));
        return;
    }

    context.popForVariable();
}

/// @brief Validate an EXIT statement against the currently active loop stack.
///
/// EXIT targets a specific loop kind (DO, WHILE, FOR).  The helper checks that a
/// loop is active and that the requested kind matches the innermost loop on the
/// stack.  It reports diagnostic B1011 when EXIT is misused or mismatched.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param stmt AST node representing the EXIT statement.
void analyzeExit(SemanticAnalyzer &analyzer, const ExitStmt &stmt)
{
    ControlCheckContext context(analyzer);
    const auto targetLoop = context.toLoopKind(stmt.kind);
    const char *targetName = context.loopKindName(targetLoop);

    if (!context.hasActiveLoop())
    {
        std::string msg = "EXIT ";
        msg += targetName;
        msg += " used outside of any loop";
        context.diagnostics().emit(
            il::support::Severity::Error, "B1011", stmt.loc, 4, std::move(msg));
        return;
    }

    const auto activeLoop = context.currentLoop();
    if (activeLoop == targetLoop)
        return;

    std::string msg = "EXIT ";
    msg += targetName;
    msg += " does not match innermost loop (";
    msg += context.loopKindName(activeLoop);
    msg += ')';
    context.diagnostics().emit(il::support::Severity::Error, "B1011", stmt.loc, 4, std::move(msg));
}

} // namespace il::frontends::basic::sem
