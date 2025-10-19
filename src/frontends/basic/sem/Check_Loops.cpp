//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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

void analyzeDo(SemanticAnalyzer &analyzer, const DoStmt &stmt)
{
    ControlCheckContext context(analyzer);
    const auto checkCond = [&]() {
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

void analyzeFor(SemanticAnalyzer &analyzer, ForStmt &stmt)
{
    ControlCheckContext context(analyzer);
    context.resolveLoopVariable(stmt.var);
    if (stmt.start)
        context.evaluateExpr(*stmt.start);
    if (stmt.end)
        context.evaluateExpr(*stmt.end);
    if (stmt.step)
        context.evaluateExpr(*stmt.step);

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

void analyzeNext(SemanticAnalyzer &analyzer, const NextStmt &stmt)
{
    ControlCheckContext context(analyzer);
    if (!context.hasForVariable() || (!stmt.var.empty() && stmt.var != context.currentForVariable()))
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
        context.diagnostics().emit(il::support::Severity::Error,
                                   "B1002",
                                   stmt.loc,
                                   4,
                                   std::move(msg));
        return;
    }

    context.popForVariable();
}

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
        context.diagnostics().emit(il::support::Severity::Error,
                                   "B1011",
                                   stmt.loc,
                                   4,
                                   std::move(msg));
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
    context.diagnostics().emit(il::support::Severity::Error,
                               "B1011",
                               stmt.loc,
                               4,
                               std::move(msg));
}

} // namespace il::frontends::basic::sem
