// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Implements BASIC semantic analyzer that collects symbols and labels
//          and validates variable usage.
// Key invariants: Symbol table reflects only definitions; unknown references
//                 produce diagnostics.
// Ownership/Lifetime: Borrowed DiagnosticEngine; AST nodes owned externally.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticAnalyzer.hpp"
#include <algorithm>
#include <limits>
#include <vector>

namespace il::frontends::basic
{

namespace
{

/// @brief Compute Levenshtein distance between strings @p a and @p b.
static size_t levenshtein(const std::string &a, const std::string &b)
{
    const size_t m = a.size();
    const size_t n = b.size();
    std::vector<size_t> prev(n + 1), cur(n + 1);
    for (size_t j = 0; j <= n; ++j)
        prev[j] = j;
    for (size_t i = 1; i <= m; ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= n; ++j)
        {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[n];
}
} // namespace

void SemanticAnalyzer::analyze(const Program &prog)
{
    symbols_.clear();
    labels_.clear();
    labelRefs_.clear();
    forStack_.clear();
    varTypes_.clear();
    arrays_.clear();
    for (const auto &stmt : prog.statements)
        if (stmt)
            labels_.insert(stmt->line);
    for (const auto &stmt : prog.statements)
        if (stmt)
            visitStmt(*stmt);
}

void SemanticAnalyzer::visitStmt(const Stmt &s)
{
    if (auto *p = dynamic_cast<const PrintStmt *>(&s))
    {
        for (const auto &it : p->items)
            if (it.kind == PrintItem::Kind::Expr && it.expr)
                visitExpr(*it.expr);
    }
    else if (auto *l = dynamic_cast<const LetStmt *>(&s))
    {
        if (auto *v = dynamic_cast<const VarExpr *>(l->target.get()))
        {
            symbols_.insert(v->name);
            if (l->expr)
                varTypes_[v->name] = visitExpr(*l->expr);
        }
        else if (auto *a = dynamic_cast<const ArrayExpr *>(l->target.get()))
        {
            if (!arrays_.count(a->name))
            {
                std::string msg = "unknown array '" + a->name + "'";
                de.emit(il::support::Severity::Error,
                        "B1001",
                        a->loc,
                        static_cast<uint32_t>(a->name.size()),
                        std::move(msg));
            }
            auto ty = visitExpr(*a->index);
            if (ty != Type::Unknown && ty != Type::Int)
            {
                std::string msg = "index type mismatch";
                de.emit(il::support::Severity::Error, "B2001", a->loc, 1, std::move(msg));
            }
            if (l->expr)
                visitExpr(*l->expr);
            auto it = arrays_.find(a->name);
            if (it != arrays_.end() && it->second >= 0)
            {
                if (auto *ci = dynamic_cast<const IntExpr *>(a->index.get()))
                {
                    if (ci->value < 0 || ci->value >= it->second)
                    {
                        std::string msg = "index out of bounds";
                        de.emit(il::support::Severity::Warning, "B3001", a->loc, 1, std::move(msg));
                    }
                }
            }
        }
    }
    else if (auto *i = dynamic_cast<const IfStmt *>(&s))
    {
        if (i->cond)
            visitExpr(*i->cond);
        if (i->then_branch)
            visitStmt(*i->then_branch);
        if (i->else_branch)
            visitStmt(*i->else_branch);
    }
    else if (auto *w = dynamic_cast<const WhileStmt *>(&s))
    {
        if (w->cond)
            visitExpr(*w->cond);
        for (const auto &bs : w->body)
            if (bs)
                visitStmt(*bs);
    }
    else if (auto *f = dynamic_cast<const ForStmt *>(&s))
    {
        symbols_.insert(f->var);
        if (f->start)
            visitExpr(*f->start);
        if (f->end)
            visitExpr(*f->end);
        if (f->step)
            visitExpr(*f->step);
        forStack_.push_back(f->var);
        for (const auto &bs : f->body)
            if (bs)
                visitStmt(*bs);
        forStack_.pop_back();
    }
    else if (auto *g = dynamic_cast<const GotoStmt *>(&s))
    {
        labelRefs_.insert(g->target);
        if (!labels_.count(g->target))
        {
            std::string msg = "unknown line " + std::to_string(g->target);
            de.emit(il::support::Severity::Error, "B1003", g->loc, 4, std::move(msg));
        }
    }
    else if (auto *n = dynamic_cast<const NextStmt *>(&s))
    {
        if (forStack_.empty() || (!n->var.empty() && n->var != forStack_.back()))
        {
            std::string msg = "mismatched NEXT";
            if (!n->var.empty())
                msg += " '" + n->var + "'";
            if (!forStack_.empty())
                msg += ", expected '" + forStack_.back() + "'";
            else
                msg += ", no active FOR";
            de.emit(il::support::Severity::Error, "B1002", n->loc, 4, std::move(msg));
        }
        else
        {
            forStack_.pop_back();
        }
    }
    else if (dynamic_cast<const EndStmt *>(&s))
    {
        // nothing
    }
    else if (auto *inp = dynamic_cast<const InputStmt *>(&s))
    {
        if (inp->prompt)
            visitExpr(*inp->prompt);
        symbols_.insert(inp->var);
        if (!inp->var.empty() && inp->var.back() == '$')
            varTypes_[inp->var] = Type::String;
        else
            varTypes_[inp->var] = Type::Int;
    }
    else if (auto *d = dynamic_cast<const DimStmt *>(&s))
    {
        auto ty = visitExpr(*d->size);
        if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "size type mismatch";
            de.emit(il::support::Severity::Error, "B2001", d->loc, 1, std::move(msg));
        }
        long long sz = -1;
        if (auto *ci = dynamic_cast<const IntExpr *>(d->size.get()))
        {
            sz = ci->value;
            if (sz <= 0)
            {
                std::string msg = "array size must be positive";
                de.emit(il::support::Severity::Error, "B2003", d->loc, 1, std::move(msg));
            }
        }
        arrays_[d->name] = sz;
    }
}

SemanticAnalyzer::Type SemanticAnalyzer::visitExpr(const Expr &e)
{
    if (dynamic_cast<const IntExpr *>(&e))
    {
        return Type::Int;
    }
    else if (dynamic_cast<const StringExpr *>(&e))
    {
        return Type::String;
    }
    else if (auto *v = dynamic_cast<const VarExpr *>(&e))
    {
        if (!symbols_.count(v->name))
        {
            std::string best;
            size_t bestDist = std::numeric_limits<size_t>::max();
            for (const auto &s : symbols_)
            {
                size_t d = levenshtein(v->name, s);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = s;
                }
            }
            std::string msg = "unknown variable '" + v->name + "'";
            if (!best.empty())
                msg += "; did you mean '" + best + "'?";
            de.emit(il::support::Severity::Error,
                    "B1001",
                    v->loc,
                    static_cast<uint32_t>(v->name.size()),
                    std::move(msg));
            return Type::Unknown;
        }
        auto it = varTypes_.find(v->name);
        if (it != varTypes_.end())
            return it->second;
        return Type::Unknown;
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&e))
    {
        Type t = Type::Unknown;
        if (u->expr)
            t = visitExpr(*u->expr);
        if (u->op == UnaryExpr::Op::Not && t == Type::String)
        {
            std::string msg = "operand type mismatch";
            de.emit(il::support::Severity::Error, "B2001", u->loc, 3, std::move(msg));
        }
        return Type::Int;
    }
    else if (auto *b = dynamic_cast<const BinaryExpr *>(&e))
    {
        Type lt = Type::Unknown;
        Type rt = Type::Unknown;
        if (b->lhs)
            lt = visitExpr(*b->lhs);
        if (b->rhs)
            rt = visitExpr(*b->rhs);
        switch (b->op)
        {
            case BinaryExpr::Op::Add:
            case BinaryExpr::Op::Sub:
            case BinaryExpr::Op::Mul:
                if ((lt != Type::Unknown && lt != Type::Int) ||
                    (rt != Type::Unknown && rt != Type::Int))
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                }
                return Type::Int;
            case BinaryExpr::Op::Div:
                if ((lt != Type::Unknown && lt != Type::Int) ||
                    (rt != Type::Unknown && rt != Type::Int))
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                }
                if (dynamic_cast<const IntExpr *>(b->lhs.get()) &&
                    dynamic_cast<const IntExpr *>(b->rhs.get()))
                {
                    auto *ri = static_cast<const IntExpr *>(b->rhs.get());
                    if (ri->value == 0)
                    {
                        std::string msg = "divide by zero";
                        de.emit(il::support::Severity::Error, "B2002", b->loc, 1, std::move(msg));
                    }
                }
                return Type::Int;
            case BinaryExpr::Op::Eq:
            case BinaryExpr::Op::Ne:
            case BinaryExpr::Op::Lt:
            case BinaryExpr::Op::Le:
            case BinaryExpr::Op::Gt:
            case BinaryExpr::Op::Ge:
                if (lt != Type::Unknown && rt != Type::Unknown && lt != rt)
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                }
                return Type::Int;
            case BinaryExpr::Op::And:
            case BinaryExpr::Op::Or:
                if ((lt != Type::Unknown && lt != Type::Int) ||
                    (rt != Type::Unknown && rt != Type::Int))
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                }
                return Type::Int;
        }
    }
    else if (auto *c = dynamic_cast<const CallExpr *>(&e))
    {
        for (const auto &a : c->args)
            if (a)
                visitExpr(*a);
        if (c->builtin == CallExpr::Builtin::Len)
            return Type::Int;
        else if (c->builtin == CallExpr::Builtin::Mid)
            return Type::String;
    }
    else if (auto *a = dynamic_cast<const ArrayExpr *>(&e))
    {
        if (!arrays_.count(a->name))
        {
            std::string msg = "unknown array '" + a->name + "'";
            de.emit(il::support::Severity::Error,
                    "B1001",
                    a->loc,
                    static_cast<uint32_t>(a->name.size()),
                    std::move(msg));
            visitExpr(*a->index);
            return Type::Unknown;
        }
        Type ty = visitExpr(*a->index);
        if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "index type mismatch";
            de.emit(il::support::Severity::Error, "B2001", a->loc, 1, std::move(msg));
        }
        auto it = arrays_.find(a->name);
        if (it != arrays_.end() && it->second >= 0)
        {
            if (auto *ci = dynamic_cast<const IntExpr *>(a->index.get()))
            {
                if (ci->value < 0 || ci->value >= it->second)
                {
                    std::string msg = "index out of bounds";
                    de.emit(il::support::Severity::Warning, "B3001", a->loc, 1, std::move(msg));
                }
            }
        }
        return Type::Int;
    }
    // Unknown expression type.
    return Type::Unknown;
}

} // namespace il::frontends::basic
