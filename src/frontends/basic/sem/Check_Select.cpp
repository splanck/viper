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

/// @brief Perform semantic validation for a @c SELECT CASE statement.
///
/// @details Establishes a @ref ControlCheckContext for the current analyzer so
///          label tracking and jump accounting remain consistent with other
///          control-flow checks.  The selector expression is classified through
///          @ref detail::classifySelectCaseSelector, which reports fatal issues
///          such as unsupported types.  Each arm is then validated and lowered
///          via @ref detail::validateSelectCaseArm and
///          @ref detail::analyzeSelectCaseBody, with the @c ELSE body analysed
///          last when present.
///
/// @param analyzer Semantic analyzer that owns global statement state.
/// @param stmt Parsed @c SELECT CASE statement under inspection.
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

/// @brief Analyse the body of a @c SELECT CASE arm using standard control-flow
///        rules.
///
/// @details Constructs a fresh @ref ControlCheckContext so nested statements can
///          reuse the common infrastructure for stack balancing, EXIT handling,
///          and diagnostic emission.  Delegates to
///          @ref detail::analyzeSelectCaseBody to keep the shared traversal in a
///          single location.
///
/// @param analyzer Semantic analyzer providing symbol and diagnostic services.
/// @param body Sequence of statements that make up the case arm.
void analyzeSelectCaseBody(SemanticAnalyzer &analyzer,
                           const std::vector<StmtPtr> &body)
{
    ControlCheckContext context(analyzer);
    detail::analyzeSelectCaseBody(context, body);
}

} // namespace il::frontends::basic::sem
