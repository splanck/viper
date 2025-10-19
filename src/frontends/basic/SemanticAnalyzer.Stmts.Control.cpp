// File: src/frontends/basic/SemanticAnalyzer.Stmts.Control.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Dispatch entry points for control-flow statement analysis in the
//          BASIC semantic analyzer.
// Key invariants: Each helper delegates to sem::check_* modules that maintain
//                 loop/label stacks via ControlCheckContext and assert balance
//                 on exit.
// Ownership/Lifetime: Borrowed SemanticAnalyzer state only.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Stmts.Control.hpp"

#include "frontends/basic/sem/Check_Common.hpp"
#include "frontends/basic/sem/Check_SelectDetail.hpp"

namespace il::frontends::basic
{

void SemanticAnalyzer::checkConditionExpr(Expr &expr)
{
    sem::checkConditionExpr(*this, expr);
}

void SemanticAnalyzer::analyzeIf(const IfStmt &stmt)
{
    sem::analyzeIf(*this, stmt);
}

void SemanticAnalyzer::analyzeSelectCase(const SelectCaseStmt &stmt)
{
    sem::analyzeSelectCase(*this, stmt);
}

void SemanticAnalyzer::analyzeSelectCaseBody(const std::vector<StmtPtr> &body)
{
    sem::analyzeSelectCaseBody(*this, body);
}

SemanticAnalyzer::SelectCaseSelectorInfo
SemanticAnalyzer::classifySelectCaseSelector(const SelectCaseStmt &stmt)
{
    sem::ControlCheckContext context(*this);
    return sem::detail::classifySelectCaseSelector(context, stmt);
}

bool SemanticAnalyzer::validateSelectCaseArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    return sem::detail::validateSelectCaseArm(arm, ctx);
}

bool SemanticAnalyzer::validateSelectCaseStringArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    return sem::detail::validateSelectCaseStringArm(arm, ctx);
}

bool SemanticAnalyzer::validateSelectCaseNumericArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    return sem::detail::validateSelectCaseNumericArm(arm, ctx);
}

void SemanticAnalyzer::analyzeWhile(const WhileStmt &stmt)
{
    sem::analyzeWhile(*this, stmt);
}

void SemanticAnalyzer::analyzeDo(const DoStmt &stmt)
{
    sem::analyzeDo(*this, stmt);
}

void SemanticAnalyzer::analyzeFor(ForStmt &stmt)
{
    sem::analyzeFor(*this, stmt);
}

void SemanticAnalyzer::analyzeGoto(const GotoStmt &stmt)
{
    sem::analyzeGoto(*this, stmt);
}

void SemanticAnalyzer::analyzeGosub(const GosubStmt &stmt)
{
    sem::analyzeGosub(*this, stmt);
}

void SemanticAnalyzer::analyzeOnErrorGoto(const OnErrorGoto &stmt)
{
    sem::analyzeOnErrorGoto(*this, stmt);
}

void SemanticAnalyzer::analyzeNext(const NextStmt &stmt)
{
    sem::analyzeNext(*this, stmt);
}

void SemanticAnalyzer::analyzeExit(const ExitStmt &stmt)
{
    sem::analyzeExit(*this, stmt);
}

void SemanticAnalyzer::analyzeResume(const Resume &stmt)
{
    sem::analyzeResume(*this, stmt);
}

void SemanticAnalyzer::analyzeReturn(ReturnStmt &stmt)
{
    sem::analyzeReturn(*this, stmt);
}

void SemanticAnalyzer::analyzeEnd(const EndStmt &)
{
    // nothing
}

void SemanticAnalyzer::installErrorHandler(int label)
{
    errorHandlerActive_ = true;
    errorHandlerTarget_ = label;
}

void SemanticAnalyzer::clearErrorHandler()
{
    errorHandlerActive_ = false;
    errorHandlerTarget_.reset();
}

bool SemanticAnalyzer::hasActiveErrorHandler() const noexcept
{
    return errorHandlerActive_;
}

} // namespace il::frontends::basic
