//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Dispatcher for SELECT CASE semantic checks.
/// @details Delegates to detail helpers that enforce selector typing and arm
///          invariants while keeping stack balance guards centralized.
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/Check_SelectDetail.hpp"

namespace il::frontends::basic::sem
{

void analyzeSelectCase(SemanticAnalyzer &analyzer, const SelectCaseStmt &stmt)
{
    ControlCheckContext context(analyzer);
    const auto selectorInfo = detail::classifySelectCaseSelector(context, stmt);
    if (selectorInfo.fatal)
        return;

    SemanticAnalyzer::SelectCaseArmContext armCtx(context.diagnostics(),
                                                  selectorInfo.selectorIsString,
                                                  selectorInfo.selectorIsNumeric,
                                                  !stmt.elseBody.empty());

    for (const auto &arm : stmt.arms)
    {
        if (!detail::validateSelectCaseArm(arm, armCtx))
            return;
        detail::analyzeSelectCaseBody(context, arm.body);
    }

    if (!stmt.elseBody.empty())
        detail::analyzeSelectCaseBody(context, stmt.elseBody);
}

void analyzeSelectCaseBody(SemanticAnalyzer &analyzer,
                           const std::vector<StmtPtr> &body)
{
    ControlCheckContext context(analyzer);
    detail::analyzeSelectCaseBody(context, body);
}

} // namespace il::frontends::basic::sem
