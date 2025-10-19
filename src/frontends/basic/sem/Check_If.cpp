//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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

void checkConditionExpr(SemanticAnalyzer &analyzer, Expr &expr)
{
    ControlCheckContext context(analyzer);
    const auto condTy = context.evaluateExpr(expr);
    using Type = SemanticAnalyzer::Type;

    if (condTy == Type::Unknown || condTy == Type::Bool)
        return;

    if (condTy == Type::Int)
    {
        if (const auto *intExpr = dynamic_cast<const IntExpr *>(&expr))
        {
            if (intExpr->value == 0 || intExpr->value == 1)
                return;
        }
    }

    std::string exprText = semantic_analyzer_detail::conditionExprText(expr);
    if (exprText.empty())
        exprText = "<expr>";

    context.diagnostics().emitNonBooleanCondition(std::string(SemanticAnalyzer::DiagNonBooleanCondition),
                                                  expr.loc,
                                                  1,
                                                  semantic_analyzer_detail::semanticTypeName(condTy),
                                                  exprText);
}

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
