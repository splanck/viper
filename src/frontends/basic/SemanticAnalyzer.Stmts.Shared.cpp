// File: src/frontends/basic/SemanticAnalyzer.Stmts.Shared.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements shared helpers for statement semantic analysis, including
//          RAII guards for loop tracking and loop-variable mutation diagnostics.
// Key invariants: Stack guards maintain analyzer invariants even when
//                 destructed via unwinding; helpers never outlive analyzer.
// Ownership/Lifetime: Helpers borrow SemanticAnalyzer references.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Stmts.Shared.hpp"

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <utility>

namespace il::frontends::basic
{

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

void SemanticAnalyzer::pushLoop(LoopKind kind)
{
    loopStack_.push_back(kind);
}

void SemanticAnalyzer::popLoop()
{
    if (!loopStack_.empty())
        loopStack_.pop_back();
}

void SemanticAnalyzer::pushForVariable(std::string name)
{
    forStack_.push_back(std::move(name));
}

void SemanticAnalyzer::popForVariable()
{
    if (!forStack_.empty())
        forStack_.pop_back();
}

bool SemanticAnalyzer::isLoopVariableActive(std::string_view name) const noexcept
{
    return std::find(forStack_.begin(), forStack_.end(), name) != forStack_.end();
}

} // namespace il::frontends::basic

namespace il::frontends::basic::semantic_analyzer_detail
{

StmtShared::StmtShared(SemanticAnalyzer &analyzer) noexcept
    : analyzer_(analyzer)
{
}

StmtShared::LoopGuard::LoopGuard(SemanticAnalyzer &analyzer, SemanticAnalyzer::LoopKind kind) noexcept
    : analyzer_(&analyzer)
{
    analyzer_->pushLoop(kind);
}

StmtShared::LoopGuard::LoopGuard(LoopGuard &&other) noexcept
    : analyzer_(other.analyzer_)
{
    other.analyzer_ = nullptr;
}

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

StmtShared::LoopGuard::~LoopGuard() noexcept
{
    if (analyzer_)
        analyzer_->popLoop();
}

StmtShared::ForLoopGuard::ForLoopGuard(SemanticAnalyzer &analyzer, std::string variable)
    : analyzer_(&analyzer)
{
    analyzer_->pushForVariable(std::move(variable));
}

StmtShared::ForLoopGuard::ForLoopGuard(ForLoopGuard &&other) noexcept
    : analyzer_(other.analyzer_)
{
    other.analyzer_ = nullptr;
}

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

StmtShared::ForLoopGuard::~ForLoopGuard() noexcept
{
    if (analyzer_)
        analyzer_->popForVariable();
}

bool StmtShared::isLoopVariable(std::string_view name) const noexcept
{
    return analyzer_.isLoopVariableActive(name);
}

void StmtShared::reportLoopVariableMutation(const std::string &name,
                                            const il::support::SourceLoc &loc,
                                            uint32_t width)
{
    std::string msg = "cannot assign to loop variable '" + name + "' inside FOR";
    analyzer_.de.emit(il::support::Severity::Error, "B1010", loc, width, std::move(msg));
}

} // namespace il::frontends::basic::semantic_analyzer_detail
