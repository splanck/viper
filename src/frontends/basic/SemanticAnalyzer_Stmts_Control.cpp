//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/frontends/basic/SemanticAnalyzer_Stmts_Control.cpp
// Purpose: Dispatch entry points for control-flow statement analysis in the
//          BASIC semantic analyzer.
// Key invariants: Each helper delegates to sem::check_* modules that maintain
//                 loop/label stacks via ControlCheckContext and assert balance
//                 on exit.
// Ownership/Lifetime: Borrowed SemanticAnalyzer state only.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SemanticAnalyzer_Stmts_Control.hpp"

#include "frontends/basic/sem/Check_Common.hpp"
#include "frontends/basic/sem/Check_SelectDetail.hpp"

namespace il::frontends::basic
{

/// @brief Type-check and normalise a condition expression.
///
/// @details Delegates to @ref sem::checkConditionExpr, which verifies BASIC
///          truthiness rules and applies implicit casts through the analyzer's
///          diagnostic machinery.
///
/// @param expr Condition expression to validate.
void SemanticAnalyzer::checkConditionExpr(Expr &expr)
{
    sem::checkConditionExpr(*this, expr);
}

/// @brief Perform semantic analysis for an IF statement tree.
///
/// @details Keeps the user-facing analyzer API thin by forwarding to
///          @ref sem::analyzeIf, which manages branch contexts, label balance,
///          and expression typing for conditional blocks.
///
/// @param stmt IF statement to analyse.
void SemanticAnalyzer::analyzeIf(const IfStmt &stmt)
{
    sem::analyzeIf(*this, stmt);
}

/// @brief Analyse the header of a SELECT CASE statement.
///
/// @details @ref sem::analyzeSelectCase performs selector typing, pushes the
///          control-flow context, and prepares case arm metadata.  This wrapper
///          provides a stable entry point for the rest of the analyzer.
///
/// @param stmt SELECT CASE statement node.
void SemanticAnalyzer::analyzeSelectCase(const SelectCaseStmt &stmt)
{
    sem::analyzeSelectCase(*this, stmt);
}

/// @brief Validate the statements nested within a SELECT CASE arm.
///
/// @details Forwards to @ref sem::analyzeSelectCaseBody so the specialised
///          checker can ensure fall-through semantics, EXIT CASE handling, and
///          nested control structures obey BASIC rules.
///
/// @param body Statements forming the SELECT CASE body.
void SemanticAnalyzer::analyzeSelectCaseBody(const std::vector<StmtPtr> &body)
{
    sem::analyzeSelectCaseBody(*this, body);
}

SemanticAnalyzer::SelectCaseSelectorInfo
/// @brief Categorise the SELECT CASE selector expression.
///
/// @details Creates a @ref sem::ControlCheckContext to capture loop/label
///          information and then defers to
///          @ref sem::detail::classifySelectCaseSelector for the core logic.
///          The resulting descriptor informs arm validation and diagnostics.
///
/// @param stmt SELECT CASE statement whose selector should be classified.
/// @return Descriptor describing selector type and comparison mode.
SemanticAnalyzer::classifySelectCaseSelector(const SelectCaseStmt &stmt)
{
    sem::ControlCheckContext context(*this);
    return sem::detail::classifySelectCaseSelector(context, stmt);
}

/// @brief Validate structural constraints shared by all SELECT CASE arms.
///
/// @details Delegates to @ref sem::detail::validateSelectCaseArm to examine
///          label uniqueness, exclusivity, and other invariants common to both
///          numeric and string selectors.
///
/// @param arm Case arm under inspection.
/// @param ctx Working context describing selector metadata.
/// @return True when the arm satisfies general constraints.
bool SemanticAnalyzer::validateSelectCaseArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    return sem::detail::validateSelectCaseArm(arm, ctx);
}

/// @brief Validate SELECT CASE arms specialised for string selectors.
///
/// @details Forwards to @ref sem::detail::validateSelectCaseStringArm, which
///          enforces case-insensitive comparisons and duplicate string checks.
///
/// @param arm String case arm to validate.
/// @param ctx Selector context describing the enclosing SELECT CASE.
/// @return True when the arm adheres to string rules.
bool SemanticAnalyzer::validateSelectCaseStringArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    return sem::detail::validateSelectCaseStringArm(arm, ctx);
}

/// @brief Validate SELECT CASE arms for numeric selectors.
///
/// @details The numeric helper checks interval ordering, overlapping ranges, and
///          implicit conversions via @ref sem::detail::validateSelectCaseNumericArm.
///
/// @param arm Numeric case arm to validate.
/// @param ctx Selector context describing the active statement.
/// @return True when numeric constraints are satisfied.
bool SemanticAnalyzer::validateSelectCaseNumericArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    return sem::detail::validateSelectCaseNumericArm(arm, ctx);
}

/// @brief Perform semantic analysis for a TRY/CATCH statement.
///
/// - Analyzes the TRY body in the current scope (no new scope).
/// - Begins a new local scope for the CATCH body.
/// - If a catch variable is present, declares it as a local of INTEGER (i64) type
///   scoped to the CATCH body. Initialization occurs during lowering.
/// - Issues a warning if both TRY and CATCH bodies are empty.
/// - Duplicate declaration of the catch variable within the same (catch) scope
///   is forbidden and reported consistently with local duplication rules.
void SemanticAnalyzer::visit(const TryCatchStmt &stmt)
{
    // Analyze TRY body under the existing scope.
    for (const auto &st : stmt.tryBody)
        if (st)
            visitStmt(*st);

    // Warn on no-op TRY/CATCH if both bodies are empty (policy: allow with warning).
    if (stmt.tryBody.empty() && stmt.catchBody.empty())
    {
        de.emit(il::support::Severity::Warning,
                "B3203",
                stmt.header.begin,
                1,
                std::string{"empty TRY and CATCH bodies"});
    }

    // Begin a new scope for the CATCH body; the optional catch variable is local to it.
    ScopeTracker::ScopedScope catchScope(scopes_);

    if (stmt.catchVar && !stmt.catchVar->empty())
    {
        const std::string &name = *stmt.catchVar;

        // Forbid duplicate declaration in the same (catch) scope.
        if (scopes_.isDeclaredInCurrentScope(name))
        {
            std::string msg = "duplicate local '" + name + "'";
            de.emit(il::support::Severity::Error,
                    "B1006",
                    stmt.header.end.isValid() ? stmt.header.end : stmt.header.begin,
                    static_cast<uint32_t>(name.size()),
                    std::move(msg));
        }
        else
        {
            // Declare a local binding for the catch variable in this scope.
            std::string unique = scopes_.declareLocal(name);

            // Track symbol and force INTEGER (i64) type; initialized during lowering.
            auto insertResult = symbols_.insert(unique);
            if (insertResult.second && activeProcScope_)
                activeProcScope_->noteSymbolInserted(unique);

            auto itType = varTypes_.find(unique);
            if (activeProcScope_)
            {
                std::optional<Type> previous;
                if (itType != varTypes_.end())
                    previous = itType->second;
                activeProcScope_->noteVarTypeMutation(unique, previous);
            }
            varTypes_[unique] = Type::Int; // INTEGER (i64)
        }
    }

    // Analyze CATCH body within the new scope.
    for (const auto &st : stmt.catchBody)
        if (st)
            visitStmt(*st);
}

/// @brief Perform semantic checks for WHILE loops.
///
/// @param stmt WHILE statement to analyse.
void SemanticAnalyzer::analyzeWhile(const WhileStmt &stmt)
{
    sem::analyzeWhile(*this, stmt);
}

/// @brief Analyse DO/LOOP constructs, including UNTIL/WHILE variants.
///
/// @param stmt DO statement node.
void SemanticAnalyzer::analyzeDo(const DoStmt &stmt)
{
    sem::analyzeDo(*this, stmt);
}

/// @brief Analyse FOR/NEXT loop semantics and iterator binding.
///
/// @param stmt FOR statement being analysed.
void SemanticAnalyzer::analyzeFor(ForStmt &stmt)
{
    sem::analyzeFor(*this, stmt);
}

/// @brief Analyse FOR EACH array iteration semantics.
///
/// @param stmt FOR EACH statement being analysed.
void SemanticAnalyzer::analyzeForEach(ForEachStmt &stmt)
{
    sem::analyzeForEach(*this, stmt);
}

/// @brief Resolve the target of a GOTO statement and validate reachability.
///
/// @param stmt GOTO statement node.
void SemanticAnalyzer::analyzeGoto(const GotoStmt &stmt)
{
    sem::analyzeGoto(*this, stmt);
}

/// @brief Analyse a GOSUB invocation, recording return expectations.
///
/// @param stmt GOSUB statement node.
void SemanticAnalyzer::analyzeGosub(const GosubStmt &stmt)
{
    sem::analyzeGosub(*this, stmt);
}

/// @brief Process an ON ERROR GOTO statement.
///
/// @param stmt ON ERROR GOTO statement to analyse.
void SemanticAnalyzer::analyzeOnErrorGoto(const OnErrorGoto &stmt)
{
    sem::analyzeOnErrorGoto(*this, stmt);
}

/// @brief Analyse a NEXT statement, pairing it with its FOR ancestor.
///
/// @param stmt NEXT statement to process.
void SemanticAnalyzer::analyzeNext(const NextStmt &stmt)
{
    sem::analyzeNext(*this, stmt);
}

/// @brief Validate EXIT statements (EXIT FOR/DO/SUB, etc.).
///
/// @param stmt EXIT statement to validate.
void SemanticAnalyzer::analyzeExit(const ExitStmt &stmt)
{
    sem::analyzeExit(*this, stmt);
}

/// @brief Analyse RESUME statements for error-handling flows.
///
/// @param stmt RESUME statement node.
void SemanticAnalyzer::analyzeResume(const Resume &stmt)
{
    sem::analyzeResume(*this, stmt);
}

/// @brief Validate RETURN statements from procedures and functions.
///
/// @param stmt RETURN statement subject to analysis.
void SemanticAnalyzer::analyzeReturn(ReturnStmt &stmt)
{
    sem::analyzeReturn(*this, stmt);

    if (!stmt.value)
        return;

    // Always visit the return expression to resolve variable names, even for
    // object-returning functions where we can't check type compatibility.
    auto valueType = visitExpr(*stmt.value);

    if (!activeFunction_ || activeFunctionExplicitRet_ == BasicType::Unknown)
        return;

    auto expected = semantic_analyzer_detail::semanticTypeFromBasic(activeFunctionExplicitRet_);
    if (!expected)
        return;
    if (valueType == Type::Unknown)
        return;

    const bool expectString = *expected == Type::String;
    const bool expectNumeric = semantic_analyzer_detail::isNumericSemanticType(*expected);
    const bool valueIsString = valueType == Type::String;
    const bool valueIsNumeric = semantic_analyzer_detail::isNumericSemanticType(valueType);

    if ((expectString && valueIsNumeric) || (expectNumeric && valueIsString))
    {
        std::string msg = "RETURN expression type ";
        msg += semantic_analyzer_detail::semanticTypeName(valueType);
        msg += " does not match declared AS ";
        msg += semantic_analyzer_detail::semanticTypeName(*expected);
        de.emit(il::support::Severity::Warning, "B4010", stmt.value->loc, 1, std::move(msg));
    }
}

/// @brief Handle END statements that terminate program execution.
///
/// @param EndStmt Unused parameter naming suppressed intentionally.
void SemanticAnalyzer::analyzeEnd(const EndStmt &)
{
    // nothing
}

/// @brief Activate an error handler targeting the supplied label.
///
/// @param label Numeric line label associated with the handler entry point.
void SemanticAnalyzer::installErrorHandler(int label)
{
    errorHandlerActive_ = true;
    errorHandlerTarget_ = label;
}

/// @brief Clear the currently installed error handler state.
void SemanticAnalyzer::clearErrorHandler()
{
    errorHandlerActive_ = false;
    errorHandlerTarget_.reset();
}

/// @brief Determine whether an ON ERROR handler is currently active.
///
/// @return True when @ref installErrorHandler has been called without a matching
///         @ref clearErrorHandler.
bool SemanticAnalyzer::hasActiveErrorHandler() const noexcept
{
    return errorHandlerActive_;
}

} // namespace il::frontends::basic
