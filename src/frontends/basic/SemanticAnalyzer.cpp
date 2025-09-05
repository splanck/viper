// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Implements BASIC semantic analyzer that collects symbols and labels,
//          validates variable usage, and registers procedures.
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

void SemanticAnalyzer::pushScope()
{
    // enter lexical scope
    scopeStack_.emplace_back();
}

void SemanticAnalyzer::popScope()
{
    // exit lexical scope
    if (!scopeStack_.empty())
        scopeStack_.pop_back();
}

std::optional<std::string> SemanticAnalyzer::resolve(const std::string &name) const
{
    for (auto it = scopeStack_.rbegin(); it != scopeStack_.rend(); ++it)
    {
        auto f = it->find(name);
        if (f != it->end())
            return f->second;
    }
    return std::nullopt;
}

void SemanticAnalyzer::analyze(const Program &prog)
{
    symbols_.clear();
    labels_.clear();
    labelRefs_.clear();
    forStack_.clear();
    varTypes_.clear();
    arrays_.clear();
    procs_.clear();
    scopeStack_.clear();
    nextLocalId_ = 0;
    for (const auto &stmt : prog.statements)
        if (stmt)
            labels_.insert(stmt->line);
    for (const auto &stmt : prog.statements)
        if (stmt)
            visitStmt(*stmt);
}

void SemanticAnalyzer::visitStmt(const Stmt &s)
{
    if (auto *lst = dynamic_cast<const StmtList *>(&s))
    {
        for (const auto &st : lst->stmts)
            if (st)
                visitStmt(*st);
    }
    else if (auto *p = dynamic_cast<const PrintStmt *>(&s))
    {
        for (const auto &it : p->items)
            if (it.kind == PrintItem::Kind::Expr && it.expr)
                visitExpr(*it.expr);
    }
    else if (auto *l = dynamic_cast<const LetStmt *>(&s))
    {
        if (auto *v = const_cast<VarExpr *>(dynamic_cast<const VarExpr *>(l->target.get())))
        {
            if (auto mapped = resolve(v->name))
                v->name = *mapped;
            symbols_.insert(v->name);
            Type varTy = Type::Int;
            if (!v->name.empty())
            {
                if (v->name.back() == '$')
                    varTy = Type::String;
                else if (v->name.back() == '#')
                    varTy = Type::Float;
            }
            if (l->expr)
            {
                Type exprTy = visitExpr(*l->expr);
                if (varTy == Type::Int && exprTy == Type::Float)
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", l->loc, 1, std::move(msg));
                }
                else if (varTy == Type::String && exprTy != Type::Unknown && exprTy != Type::String)
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", l->loc, 1, std::move(msg));
                }
            }
            varTypes_[v->name] = varTy;
        }
        else if (auto *a =
                     const_cast<ArrayExpr *>(dynamic_cast<const ArrayExpr *>(l->target.get())))
        {
            if (auto mapped = resolve(a->name))
                a->name = *mapped;
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
        {
            pushScope(); // enter scope
            visitStmt(*i->then_branch);
            popScope(); // exit scope
        }
        for (const auto &e : i->elseifs)
        {
            if (e.cond)
                visitExpr(*e.cond);
            if (e.then_branch)
            {
                pushScope(); // enter scope
                visitStmt(*e.then_branch);
                popScope(); // exit scope
            }
        }
        if (i->else_branch)
        {
            pushScope(); // enter scope
            visitStmt(*i->else_branch);
            popScope(); // exit scope
        }
    }
    else if (auto *w = dynamic_cast<const WhileStmt *>(&s))
    {
        if (w->cond)
            visitExpr(*w->cond);
        pushScope(); // enter scope
        for (const auto &bs : w->body)
            if (bs)
                visitStmt(*bs);
        popScope(); // exit scope
    }
    else if (auto *f = dynamic_cast<const ForStmt *>(&s))
    {
        auto *fc = const_cast<ForStmt *>(f);
        if (auto mapped = resolve(fc->var))
            fc->var = *mapped;
        symbols_.insert(fc->var);
        if (f->start)
            visitExpr(*f->start);
        if (f->end)
            visitExpr(*f->end);
        if (f->step)
            visitExpr(*f->step);
        forStack_.push_back(fc->var);
        pushScope(); // enter scope
        for (const auto &bs : f->body)
            if (bs)
                visitStmt(*bs);
        popScope(); // exit scope
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
    else if (auto *r = dynamic_cast<const RandomizeStmt *>(&s))
    {
        if (r->seed)
        {
            auto ty = visitExpr(*r->seed);
            if (ty != Type::Unknown && ty != Type::Int && ty != Type::Float)
            {
                std::string msg = "seed type mismatch";
                de.emit(il::support::Severity::Error, "B2001", r->loc, 1, std::move(msg));
            }
        }
    }
    else if (auto *inp = dynamic_cast<const InputStmt *>(&s))
    {
        if (inp->prompt)
            visitExpr(*inp->prompt);
        auto *ic = const_cast<InputStmt *>(inp);
        if (auto mapped = resolve(ic->var))
            ic->var = *mapped;
        symbols_.insert(ic->var);
        if (!ic->var.empty() && ic->var.back() == '$')
            varTypes_[ic->var] = Type::String;
        else if (!ic->var.empty() && ic->var.back() == '#')
            varTypes_[ic->var] = Type::Float;
        else
            varTypes_[ic->var] = Type::Int;
    }
    else if (auto *d = dynamic_cast<const DimStmt *>(&s))
    {
        auto *dc = const_cast<DimStmt *>(d);
        auto ty = visitExpr(*dc->size);
        if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "size type mismatch";
            de.emit(il::support::Severity::Error, "B2001", dc->loc, 1, std::move(msg));
        }
        long long sz = -1;
        if (auto *ci = dynamic_cast<const IntExpr *>(dc->size.get()))
        {
            sz = ci->value;
            if (sz <= 0)
            {
                std::string msg = "array size must be positive";
                de.emit(il::support::Severity::Error, "B2003", dc->loc, 1, std::move(msg));
            }
        }
        if (!scopeStack_.empty())
        {
            auto &cur = scopeStack_.back();
            if (cur.count(dc->name))
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
                std::string unique = dc->name + "_" + std::to_string(nextLocalId_++);
                cur[dc->name] = unique;
                dc->name = unique;
                symbols_.insert(unique);
            }
        }
        arrays_[dc->name] = sz;
    }
    else if (auto *f = dynamic_cast<const FunctionDecl *>(&s))
    {
        if (procs_.count(f->name))
        {
            std::string msg = "duplicate procedure '" + f->name + "'";
            de.emit(il::support::Severity::Error,
                    "B1004",
                    f->loc,
                    static_cast<uint32_t>(f->name.size()),
                    std::move(msg));
        }
        else
        {
            ProcSignature sig;
            sig.kind = ProcSignature::Kind::Function;
            sig.retType = f->ret;
            std::unordered_set<std::string> paramNames;
            for (const auto &p : f->params)
            {
                if (!paramNames.insert(p.name).second)
                {
                    std::string msg = "duplicate parameter '" + p.name + "'";
                    de.emit(il::support::Severity::Error,
                            "B1005",
                            p.loc,
                            static_cast<uint32_t>(p.name.size()),
                            std::move(msg));
                }
                if (p.is_array && p.type != ::il::frontends::basic::Type::I64 &&
                    p.type != ::il::frontends::basic::Type::Str)
                {
                    std::string msg = "array parameter must be i64 or str";
                    de.emit(il::support::Severity::Error,
                            "B2004",
                            p.loc,
                            static_cast<uint32_t>(p.name.size()),
                            std::move(msg));
                }
                sig.params.push_back({p.type, p.is_array});
            }
            procs_.emplace(f->name, std::move(sig));

            auto symSave = symbols_;
            auto typeSave = varTypes_;
            auto arrSave = arrays_;
            auto labelSave = labels_;
            auto labelRefSave = labelRefs_;
            auto forSave = forStack_;
            pushScope(); // enter function scope
            for (const auto &p : f->params)
            {
                scopeStack_.back()[p.name] = p.name;
                symbols_.insert(p.name);
                SemanticAnalyzer::Type vt = SemanticAnalyzer::Type::Int;
                if (p.type == ::il::frontends::basic::Type::Str)
                    vt = SemanticAnalyzer::Type::String;
                else if (p.type == ::il::frontends::basic::Type::F64)
                    vt = SemanticAnalyzer::Type::Float;
                varTypes_[p.name] = vt;
                if (p.is_array)
                    arrays_[p.name] = -1;
            }
            for (const auto &st : f->body)
                if (st)
                    visitStmt(*st);
            popScope(); // exit function scope
            symbols_ = std::move(symSave);
            varTypes_ = std::move(typeSave);
            arrays_ = std::move(arrSave);
            labels_ = std::move(labelSave);
            labelRefs_ = std::move(labelRefSave);
            forStack_ = std::move(forSave);
        }
    }
    else if (auto *sub = dynamic_cast<const SubDecl *>(&s))
    {
        if (procs_.count(sub->name))
        {
            std::string msg = "duplicate procedure '" + sub->name + "'";
            de.emit(il::support::Severity::Error,
                    "B1004",
                    sub->loc,
                    static_cast<uint32_t>(sub->name.size()),
                    std::move(msg));
        }
        else
        {
            ProcSignature sig;
            sig.kind = ProcSignature::Kind::Sub;
            sig.retType = ::il::frontends::basic::Type::I64;
            std::unordered_set<std::string> paramNames;
            for (const auto &p : sub->params)
            {
                if (!paramNames.insert(p.name).second)
                {
                    std::string msg = "duplicate parameter '" + p.name + "'";
                    de.emit(il::support::Severity::Error,
                            "B1005",
                            p.loc,
                            static_cast<uint32_t>(p.name.size()),
                            std::move(msg));
                }
                if (p.is_array && p.type != ::il::frontends::basic::Type::I64 &&
                    p.type != ::il::frontends::basic::Type::Str)
                {
                    std::string msg = "array parameter must be i64 or str";
                    de.emit(il::support::Severity::Error,
                            "B2004",
                            p.loc,
                            static_cast<uint32_t>(p.name.size()),
                            std::move(msg));
                }
                sig.params.push_back({p.type, p.is_array});
            }
            procs_.emplace(sub->name, std::move(sig));

            auto symSave = symbols_;
            auto typeSave = varTypes_;
            auto arrSave = arrays_;
            auto labelSave = labels_;
            auto labelRefSave = labelRefs_;
            auto forSave = forStack_;
            pushScope(); // enter subroutine scope
            for (const auto &p : sub->params)
            {
                scopeStack_.back()[p.name] = p.name;
                symbols_.insert(p.name);
                SemanticAnalyzer::Type vt = SemanticAnalyzer::Type::Int;
                if (p.type == ::il::frontends::basic::Type::Str)
                    vt = SemanticAnalyzer::Type::String;
                else if (p.type == ::il::frontends::basic::Type::F64)
                    vt = SemanticAnalyzer::Type::Float;
                varTypes_[p.name] = vt;
                if (p.is_array)
                    arrays_[p.name] = -1;
            }
            for (const auto &st : sub->body)
                if (st)
                    visitStmt(*st);
            popScope(); // exit subroutine scope
            symbols_ = std::move(symSave);
            varTypes_ = std::move(typeSave);
            arrays_ = std::move(arrSave);
            labels_ = std::move(labelSave);
            labelRefs_ = std::move(labelRefSave);
            forStack_ = std::move(forSave);
        }
    }
}

SemanticAnalyzer::Type SemanticAnalyzer::visitExpr(const Expr &e)
{
    if (dynamic_cast<const IntExpr *>(&e))
    {
        return Type::Int;
    }
    else if (dynamic_cast<const FloatExpr *>(&e))
    {
        return Type::Float;
    }
    else if (dynamic_cast<const StringExpr *>(&e))
    {
        return Type::String;
    }
    else if (auto *v = dynamic_cast<const VarExpr *>(&e))
    {
        auto *vc = const_cast<VarExpr *>(v);
        if (auto mapped = resolve(vc->name))
            vc->name = *mapped;
        if (!symbols_.count(vc->name))
        {
            std::string best;
            size_t bestDist = std::numeric_limits<size_t>::max();
            for (const auto &s : symbols_)
            {
                size_t d = levenshtein(vc->name, s);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = s;
                }
            }
            std::string msg = "unknown variable '" + vc->name + "'";
            if (!best.empty())
                msg += "; did you mean '" + best + "'?";
            de.emit(il::support::Severity::Error,
                    "B1001",
                    vc->loc,
                    static_cast<uint32_t>(vc->name.size()),
                    std::move(msg));
            return Type::Unknown;
        }
        auto it = varTypes_.find(vc->name);
        if (it != varTypes_.end())
            return it->second;
        if (!vc->name.empty())
        {
            if (vc->name.back() == '$')
                return Type::String;
            if (vc->name.back() == '#')
                return Type::Float;
        }
        return Type::Int;
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&e))
    {
        Type t = Type::Unknown;
        if (u->expr)
            t = visitExpr(*u->expr);
        if (u->op == UnaryExpr::Op::Not && t != Type::Unknown && t != Type::Int)
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
        auto isNum = [](Type t)
        { return t == Type::Int || t == Type::Float || t == Type::Unknown; };
        switch (b->op)
        {
            case BinaryExpr::Op::Add:
            case BinaryExpr::Op::Sub:
            case BinaryExpr::Op::Mul:
            {
                if (!isNum(lt) || !isNum(rt))
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                }
                return (lt == Type::Float || rt == Type::Float) ? Type::Float : Type::Int;
            }
            case BinaryExpr::Op::Div:
            {
                if (!isNum(lt) || !isNum(rt))
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                }
                if (lt == Type::Float || rt == Type::Float)
                    return Type::Float;
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
            }
            case BinaryExpr::Op::IDiv:
            case BinaryExpr::Op::Mod:
            {
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
            }
            case BinaryExpr::Op::Eq:
            case BinaryExpr::Op::Ne:
            case BinaryExpr::Op::Lt:
            case BinaryExpr::Op::Le:
            case BinaryExpr::Op::Gt:
            case BinaryExpr::Op::Ge:
            {
                bool lNum = lt == Type::Int || lt == Type::Float;
                bool rNum = rt == Type::Int || rt == Type::Float;
                if (lt == Type::String && rt == Type::String)
                {
                    if (b->op == BinaryExpr::Op::Lt || b->op == BinaryExpr::Op::Le ||
                        b->op == BinaryExpr::Op::Gt || b->op == BinaryExpr::Op::Ge)
                    {
                        std::string msg = "operand type mismatch";
                        de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                    }
                    return Type::Int;
                }
                if (lNum && rNum)
                    return Type::Int;
                if (lt != Type::Unknown && rt != Type::Unknown && lt != rt)
                {
                    std::string msg = "operand type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", b->loc, 1, std::move(msg));
                }
                return Type::Int;
            }
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
    else if (auto *c = dynamic_cast<const BuiltinCallExpr *>(&e))
    {
        std::vector<Type> argTys;
        argTys.reserve(c->args.size());
        for (const auto &a : c->args)
            argTys.push_back(a ? visitExpr(*a) : Type::Unknown);
        auto err = [&](size_t idx)
        {
            std::string msg = "argument type mismatch";
            il::support::SourceLoc loc = c->loc;
            if (idx < c->args.size() && c->args[idx])
                loc = c->args[idx]->loc;
            de.emit(il::support::Severity::Error, "B2001", loc, 1, std::move(msg));
        };
        if (c->builtin == BuiltinCallExpr::Builtin::Len)
        {
            if (argTys.size() != 1 || (argTys[0] != Type::Unknown && argTys[0] != Type::String))
                err(0);
            return Type::Int;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Mid)
        {
            if (argTys.size() < 2 || argTys.size() > 3)
                err(0);
            if (argTys.size() >= 1 && argTys[0] != Type::Unknown && argTys[0] != Type::String)
                err(0);
            if (argTys.size() >= 2 && argTys[1] != Type::Unknown && argTys[1] != Type::Int)
                err(1);
            if (argTys.size() == 3 && argTys[2] != Type::Unknown && argTys[2] != Type::Int)
                err(2);
            return Type::String;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Left ||
                 c->builtin == BuiltinCallExpr::Builtin::Right)
        {
            if (argTys.size() != 2)
                err(0);
            if (argTys.size() >= 1 && argTys[0] != Type::Unknown && argTys[0] != Type::String)
                err(0);
            if (argTys.size() >= 2 && argTys[1] != Type::Unknown && argTys[1] != Type::Int)
                err(1);
            return Type::String;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Str)
        {
            if (argTys.size() != 1)
                err(0);
            if (argTys.size() >= 1 && argTys[0] != Type::Unknown && argTys[0] != Type::Int &&
                argTys[0] != Type::Float)
                err(0);
            return Type::String;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Val)
        {
            if (argTys.size() != 1)
                err(0);
            if (argTys.size() >= 1 && argTys[0] != Type::Unknown && argTys[0] != Type::String)
                err(0);
            return Type::Int;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Int)
        {
            if (argTys.size() != 1)
                err(0);
            if (argTys.size() >= 1 && argTys[0] != Type::Unknown && argTys[0] != Type::Float)
                err(0);
            return Type::Int;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Sqr)
        {
            if (argTys.size() != 1 ||
                (argTys[0] != Type::Unknown && argTys[0] != Type::Int && argTys[0] != Type::Float))
                err(0);
            return Type::Float;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Abs)
        {
            if (argTys.size() != 1)
                err(0);
            Type t = argTys[0];
            if (t == Type::Float)
                return Type::Float;
            if (t == Type::Int || t == Type::Unknown)
                return Type::Int;
            err(0);
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Floor ||
                 c->builtin == BuiltinCallExpr::Builtin::Ceil)
        {
            if (argTys.size() != 1 ||
                (argTys[0] != Type::Unknown && argTys[0] != Type::Int && argTys[0] != Type::Float))
                err(0);
            return Type::Float;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Sin ||
                 c->builtin == BuiltinCallExpr::Builtin::Cos)
        {
            if (argTys.size() != 1 ||
                (argTys[0] != Type::Unknown && argTys[0] != Type::Int && argTys[0] != Type::Float))
                err(0);
            return Type::Float;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Pow)
        {
            if (argTys.size() != 2)
                err(0);
            if (argTys.size() >= 1 && argTys[0] != Type::Unknown && argTys[0] != Type::Int &&
                argTys[0] != Type::Float)
                err(0);
            if (argTys.size() >= 2 && argTys[1] != Type::Unknown && argTys[1] != Type::Int &&
                argTys[1] != Type::Float)
                err(1);
            return Type::Float;
        }
        else if (c->builtin == BuiltinCallExpr::Builtin::Rnd)
        {
            if (!argTys.empty())
                err(0);
            return Type::Float;
        }
    }
    else if (auto *c = dynamic_cast<const CallExpr *>(&e))
    {
        auto it = procs_.find(c->callee);
        if (it == procs_.end())
        {
            std::string msg = "unknown procedure '" + c->callee + "'";
            de.emit(il::support::Severity::Error,
                    "B1006",
                    c->loc,
                    static_cast<uint32_t>(c->callee.size()),
                    std::move(msg));
            for (auto &a : c->args)
                if (a)
                    visitExpr(*a);
            return Type::Unknown;
        }
        const ProcSignature &sig = it->second;
        if (sig.kind == ProcSignature::Kind::Sub)
        {
            std::string msg = "subroutine '" + c->callee + "' used in expression";
            de.emit(il::support::Severity::Error,
                    "B2005",
                    c->loc,
                    static_cast<uint32_t>(c->callee.size()),
                    std::move(msg));
            for (auto &a : c->args)
                if (a)
                    visitExpr(*a);
            return Type::Unknown;
        }
        std::vector<Type> argTys;
        for (auto &a : c->args)
            argTys.push_back(a ? visitExpr(*a) : Type::Unknown);
        if (argTys.size() != sig.params.size())
        {
            std::string msg = "wrong number of arguments";
            de.emit(il::support::Severity::Error, "B2005", c->loc, 1, std::move(msg));
        }
        size_t n = std::min(argTys.size(), sig.params.size());
        for (size_t i = 0; i < n; ++i)
        {
            auto expectTy = sig.params[i].type;
            auto argTy = argTys[i];
            if (sig.params[i].is_array)
            {
                if (auto *v = dynamic_cast<VarExpr *>(c->args[i].get()))
                {
                    if (!arrays_.count(v->name))
                    {
                        std::string msg = "array argument must be array variable";
                        de.emit(il::support::Severity::Error,
                                "B2006",
                                c->loc,
                                static_cast<uint32_t>(c->callee.size()),
                                std::move(msg));
                    }
                }
                else
                {
                    std::string msg = "array argument must be array variable";
                    de.emit(il::support::Severity::Error,
                            "B2006",
                            c->loc,
                            static_cast<uint32_t>(c->callee.size()),
                            std::move(msg));
                }
                continue;
            }
            if (expectTy == ::il::frontends::basic::Type::F64 && argTy == Type::Int)
                continue;
            Type want = Type::Int;
            if (expectTy == ::il::frontends::basic::Type::F64)
                want = Type::Float;
            else if (expectTy == ::il::frontends::basic::Type::Str)
                want = Type::String;
            if (argTy != Type::Unknown && argTy != want)
            {
                std::string msg = "argument type mismatch";
                de.emit(il::support::Severity::Error, "B2001", c->loc, 1, std::move(msg));
            }
        }
        if (sig.retType == ::il::frontends::basic::Type::F64)
            return Type::Float;
        if (sig.retType == ::il::frontends::basic::Type::Str)
            return Type::String;
        return Type::Int;
    }
    else if (auto *a = dynamic_cast<const ArrayExpr *>(&e))
    {
        auto *ac = const_cast<ArrayExpr *>(a);
        if (auto mapped = resolve(ac->name))
            ac->name = *mapped;
        if (!arrays_.count(ac->name))
        {
            std::string msg = "unknown array '" + ac->name + "'";
            de.emit(il::support::Severity::Error,
                    "B1001",
                    ac->loc,
                    static_cast<uint32_t>(ac->name.size()),
                    std::move(msg));
            visitExpr(*ac->index);
            return Type::Unknown;
        }
        Type ty = visitExpr(*ac->index);
        if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "index type mismatch";
            de.emit(il::support::Severity::Error, "B2001", ac->loc, 1, std::move(msg));
        }
        auto it = arrays_.find(ac->name);
        if (it != arrays_.end() && it->second >= 0)
        {
            if (auto *ci = dynamic_cast<const IntExpr *>(ac->index.get()))
            {
                if (ci->value < 0 || ci->value >= it->second)
                {
                    std::string msg = "index out of bounds";
                    de.emit(il::support::Severity::Warning, "B3001", ac->loc, 1, std::move(msg));
                }
            }
        }
        return Type::Int;
    }
    // Unknown expression type.
    return Type::Unknown;
}

} // namespace il::frontends::basic
