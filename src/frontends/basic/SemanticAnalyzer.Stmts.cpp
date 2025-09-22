// File: src/frontends/basic/SemanticAnalyzer.Stmts.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements statement-level analysis for the BASIC semantic analyzer,
//          covering symbol tracking, control-flow validation, and declaration
//          handling.
// Key invariants: Statement visitors propagate scope information and emit
//                 diagnostics for invalid constructs.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes remain
//                     owned externally.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

namespace il::frontends::basic
{

using semantic_analyzer_detail::astToSemanticType;
using semantic_analyzer_detail::conditionExprText;
using semantic_analyzer_detail::semanticTypeName;

class SemanticAnalyzerStmtVisitor final : public StmtVisitor
{
  public:
    explicit SemanticAnalyzerStmtVisitor(SemanticAnalyzer &analyzer) noexcept
        : analyzer_(analyzer)
    {
    }

    void visit(const PrintStmt &stmt) override { analyzer_.analyzePrint(stmt); }
    void visit(const LetStmt &stmt) override { analyzer_.analyzeLet(stmt); }
    void visit(const DimStmt &stmt) override { analyzer_.analyzeDim(stmt); }
    void visit(const RandomizeStmt &stmt) override { analyzer_.analyzeRandomize(stmt); }
    void visit(const IfStmt &stmt) override { analyzer_.analyzeIf(stmt); }
    void visit(const WhileStmt &stmt) override { analyzer_.analyzeWhile(stmt); }
    void visit(const ForStmt &stmt) override { analyzer_.analyzeFor(stmt); }
    void visit(const NextStmt &stmt) override { analyzer_.analyzeNext(stmt); }
    void visit(const GotoStmt &stmt) override { analyzer_.analyzeGoto(stmt); }
    void visit(const EndStmt &stmt) override { analyzer_.analyzeEnd(stmt); }
    void visit(const InputStmt &stmt) override { analyzer_.analyzeInput(stmt); }
    void visit(const ReturnStmt &) override {}
    void visit(const FunctionDecl &) override {}
    void visit(const SubDecl &) override {}
    void visit(const StmtList &stmt) override { analyzer_.analyzeStmtList(stmt); }

  private:
    SemanticAnalyzer &analyzer_;
};

void SemanticAnalyzer::visitStmt(const Stmt &s)
{
    SemanticAnalyzerStmtVisitor visitor(*this);
    s.accept(visitor);
}

void SemanticAnalyzer::analyzeStmtList(const StmtList &lst)
{
    for (const auto &st : lst.stmts)
        if (st)
            visitStmt(*st);
}

void SemanticAnalyzer::analyzePrint(const PrintStmt &p)
{
    for (const auto &it : p.items)
        if (it.kind == PrintItem::Kind::Expr && it.expr)
            visitExpr(*it.expr);
}

void SemanticAnalyzer::analyzeVarAssignment(VarExpr &v, const LetStmt &l)
{
    resolveAndTrackSymbol(v.name, SymbolKind::Definition);
    Type varTy = Type::Int;
    if (auto itType = varTypes_.find(v.name); itType != varTypes_.end())
        varTy = itType->second;
    if (l.expr)
    {
        Type exprTy = visitExpr(*l.expr);
        if (varTy == Type::Int && exprTy == Type::Float)
        {
            std::string msg = "operand type mismatch";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
        else if (varTy == Type::String && exprTy != Type::Unknown && exprTy != Type::String)
        {
            std::string msg = "operand type mismatch";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
        else if (varTy == Type::Bool && exprTy != Type::Unknown && exprTy != Type::Bool)
        {
            std::string msg = "operand type mismatch";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
    }
}

void SemanticAnalyzer::analyzeArrayAssignment(ArrayExpr &a, const LetStmt &l)
{
    resolveAndTrackSymbol(a.name, SymbolKind::Reference);
    if (!arrays_.count(a.name))
    {
        std::string msg = "unknown array '" + a.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::move(msg));
    }
    auto ty = visitExpr(*a.index);
    if (ty != Type::Unknown && ty != Type::Int)
    {
        std::string msg = "index type mismatch";
        de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
    }
    if (l.expr)
        visitExpr(*l.expr);
    auto it = arrays_.find(a.name);
    if (it != arrays_.end() && it->second >= 0)
    {
        if (auto *ci = dynamic_cast<const IntExpr *>(a.index.get()))
        {
            if (ci->value < 0 || ci->value >= it->second)
            {
                std::string msg = "index out of bounds";
                de.emit(il::support::Severity::Warning, "B3001", a.loc, 1, std::move(msg));
            }
        }
    }
}

void SemanticAnalyzer::analyzeConstExpr(const LetStmt &l)
{
    if (l.target)
        visitExpr(*l.target);
    if (l.expr)
        visitExpr(*l.expr);
    std::string msg = "left-hand side of LET must be a variable or array element";
    de.emit(il::support::Severity::Error, "B2007", l.loc, 1, std::move(msg));
}

void SemanticAnalyzer::analyzeLet(const LetStmt &l)
{
    if (!l.target)
        return;
    if (auto *v = const_cast<VarExpr *>(dynamic_cast<const VarExpr *>(l.target.get())))
    {
        analyzeVarAssignment(*v, l);
    }
    else if (auto *a = const_cast<ArrayExpr *>(dynamic_cast<const ArrayExpr *>(l.target.get())))
    {
        analyzeArrayAssignment(*a, l);
    }
    else
    {
        analyzeConstExpr(l);
    }
}

void SemanticAnalyzer::checkConditionExpr(const Expr &expr)
{
    Type condTy = visitExpr(expr);
    if (condTy == Type::Unknown || condTy == Type::Bool)
        return;

    if (condTy == Type::Int)
    {
        if (auto *intExpr = dynamic_cast<const IntExpr *>(&expr))
        {
            if (intExpr->value == 0 || intExpr->value == 1)
                return;
        }
    }

    std::string exprText = conditionExprText(expr);
    if (exprText.empty())
        exprText = "<expr>";

    de.emitNonBooleanCondition(std::string(DiagNonBooleanCondition),
                               expr.loc,
                               1,
                               semanticTypeName(condTy),
                               exprText);
}

void SemanticAnalyzer::analyzeIf(const IfStmt &i)
{
    if (i.cond)
        checkConditionExpr(*i.cond);
    if (i.then_branch)
    {
        ScopeTracker::ScopedScope scope(scopes_);
        visitStmt(*i.then_branch);
    }
    for (const auto &e : i.elseifs)
    {
        if (e.cond)
            checkConditionExpr(*e.cond);
        if (e.then_branch)
        {
            ScopeTracker::ScopedScope scope(scopes_);
            visitStmt(*e.then_branch);
        }
    }
    if (i.else_branch)
    {
        ScopeTracker::ScopedScope scope(scopes_);
        visitStmt(*i.else_branch);
    }
}

void SemanticAnalyzer::analyzeWhile(const WhileStmt &w)
{
    if (w.cond)
        checkConditionExpr(*w.cond);
    ScopeTracker::ScopedScope scope(scopes_);
    for (const auto &bs : w.body)
        if (bs)
            visitStmt(*bs);
}

void SemanticAnalyzer::analyzeFor(const ForStmt &f)
{
    auto *fc = const_cast<ForStmt *>(&f);
    resolveAndTrackSymbol(fc->var, SymbolKind::Definition);
    if (f.start)
        visitExpr(*f.start);
    if (f.end)
        visitExpr(*f.end);
    if (f.step)
        visitExpr(*f.step);
    forStack_.push_back(fc->var);
    {
        ScopeTracker::ScopedScope scope(scopes_);
        for (const auto &bs : f.body)
            if (bs)
                visitStmt(*bs);
    }
    forStack_.pop_back();
}

void SemanticAnalyzer::analyzeGoto(const GotoStmt &g)
{
    labelRefs_.insert(g.target);
    if (!labels_.count(g.target))
    {
        std::string msg = "unknown line " + std::to_string(g.target);
        de.emit(il::support::Severity::Error, "B1003", g.loc, 4, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeNext(const NextStmt &n)
{
    if (forStack_.empty() || (!n.var.empty() && n.var != forStack_.back()))
    {
        std::string msg = "mismatched NEXT";
        if (!n.var.empty())
            msg += " '" + n.var + "'";
        if (!forStack_.empty())
            msg += ", expected '" + forStack_.back() + "'";
        else
            msg += ", no active FOR";
        de.emit(il::support::Severity::Error, "B1002", n.loc, 4, std::move(msg));
    }
    else
    {
        forStack_.pop_back();
    }
}

void SemanticAnalyzer::analyzeEnd(const EndStmt &)
{
    // nothing
}

void SemanticAnalyzer::analyzeRandomize(const RandomizeStmt &r)
{
    if (r.seed)
    {
        auto ty = visitExpr(*r.seed);
        if (ty != Type::Unknown && ty != Type::Int && ty != Type::Float)
        {
            std::string msg = "seed type mismatch";
            de.emit(il::support::Severity::Error, "B2001", r.loc, 1, std::move(msg));
        }
    }
}

void SemanticAnalyzer::analyzeInput(const InputStmt &inp)
{
    if (inp.prompt)
        visitExpr(*inp.prompt);
    auto *ic = const_cast<InputStmt *>(&inp);
    resolveAndTrackSymbol(ic->var, SymbolKind::InputTarget);
}

void SemanticAnalyzer::analyzeDim(const DimStmt &d)
{
    auto *dc = const_cast<DimStmt *>(&d);
    long long sz = -1;
    if (dc->isArray)
    {
        if (dc->size)
        {
            auto ty = visitExpr(*dc->size);
            if (ty != Type::Unknown && ty != Type::Int)
            {
                std::string msg = "size type mismatch";
                de.emit(il::support::Severity::Error, "B2001", dc->loc, 1, std::move(msg));
            }
            if (auto *ci = dynamic_cast<const IntExpr *>(dc->size.get()))
            {
                sz = ci->value;
                if (sz <= 0)
                {
                    std::string msg = "array size must be positive";
                    de.emit(il::support::Severity::Error, "B2003", dc->loc, 1, std::move(msg));
                }
            }
        }
    }
    if (scopes_.hasScope())
    {
        if (scopes_.isDeclaredInCurrentScope(dc->name))
        {
            std::string msg = "duplicate local '" + dc->name + "'";
            de.emit(il::support::Severity::Error,
                    "B1006",
                    dc->loc,
                    static_cast<uint32_t>(dc->name.size()),
                    std::move(msg));
        }
        else
        {
            std::string unique = scopes_.declareLocal(dc->name);
            dc->name = unique;
            symbols_.insert(unique);
        }
    }
    else
    {
        symbols_.insert(dc->name);
    }
    if (dc->isArray)
    {
        arrays_[dc->name] = sz;
    }
    else
    {
        varTypes_[dc->name] = astToSemanticType(dc->type);
    }
}

} // namespace il::frontends::basic
