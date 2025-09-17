// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Implements BASIC semantic analyzer that collects symbols and labels,
//          validates variable usage, and performs two-pass procedure
//          registration.
// Key invariants: Symbol table reflects only definitions; unknown references
//                 produce diagnostics.
// Ownership/Lifetime: Borrowed DiagnosticEngine; AST nodes owned externally.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticAnalyzer.hpp"
#include <algorithm>
#include <limits>
#include <sstream>
#include <typeindex>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

namespace
{
template <typename T, void (SemanticAnalyzer::*Fn)(const T &)>
void dispatchHelper(SemanticAnalyzer *sa, const Stmt &s)
{
    (sa->*Fn)(static_cast<const T &>(s));
}

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

static SemanticAnalyzer::Type astToSemanticType(::il::frontends::basic::Type ty)
{
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return SemanticAnalyzer::Type::Int;
        case ::il::frontends::basic::Type::F64:
            return SemanticAnalyzer::Type::Float;
        case ::il::frontends::basic::Type::Str:
            return SemanticAnalyzer::Type::String;
        case ::il::frontends::basic::Type::Bool:
            return SemanticAnalyzer::Type::Bool;
    }
    return SemanticAnalyzer::Type::Int;
}

/// @brief Convert builtin enum to BASIC name.
static const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

static const char *semanticTypeName(SemanticAnalyzer::Type type)
{
    using Type = SemanticAnalyzer::Type;
    switch (type)
    {
        case Type::Int:
            return "INT";
        case Type::Float:
            return "FLOAT";
        case Type::String:
            return "STRING";
        case Type::Bool:
            return "BOOLEAN";
        case Type::Unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

static const char *logicalOpName(BinaryExpr::Op op)
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAndShort:
            return "ANDALSO";
        case BinaryExpr::Op::LogicalOrShort:
            return "ORELSE";
        case BinaryExpr::Op::LogicalAnd:
            return "AND";
        case BinaryExpr::Op::LogicalOr:
            return "OR";
        default:
            break;
    }
    return "<logical>";
}

} // namespace

void SemanticAnalyzer::analyzeProc(const FunctionDecl &f)
{
    auto symSave = symbols_;
    auto typeSave = varTypes_;
    auto arrSave = arrays_;
    auto labelSave = labels_;
    auto labelRefSave = labelRefs_;
    auto forSave = forStack_;
    scopes_.pushScope();
    for (const auto &p : f.params)
    {
        scopes_.bind(p.name, p.name);
        symbols_.insert(p.name);
        SemanticAnalyzer::Type vt = SemanticAnalyzer::Type::Int;
        if (p.type == ::il::frontends::basic::Type::Str)
            vt = SemanticAnalyzer::Type::String;
        else if (p.type == ::il::frontends::basic::Type::F64)
            vt = SemanticAnalyzer::Type::Float;
        else if (p.type == ::il::frontends::basic::Type::Bool)
            vt = SemanticAnalyzer::Type::Bool;
        varTypes_[p.name] = vt;
        if (p.is_array)
            arrays_[p.name] = -1;
    }
    for (const auto &st : f.body)
        if (st)
            visitStmt(*st);
    bool allPathsReturn = mustReturn(f.body);
    scopes_.popScope();
    symbols_ = std::move(symSave);
    varTypes_ = std::move(typeSave);
    arrays_ = std::move(arrSave);
    labels_ = std::move(labelSave);
    labelRefs_ = std::move(labelRefSave);
    forStack_ = std::move(forSave);
    if (!allPathsReturn)
    {
        std::string msg = "missing return in FUNCTION " + f.name;
        de.emit(il::support::Severity::Error,
                "B1007",
                f.endLoc.isValid() ? f.endLoc : f.loc,
                3,
                std::move(msg));
    }
}

void SemanticAnalyzer::analyzeProc(const SubDecl &s)
{
    auto symSave = symbols_;
    auto typeSave = varTypes_;
    auto arrSave = arrays_;
    auto labelSave = labels_;
    auto labelRefSave = labelRefs_;
    auto forSave = forStack_;
    scopes_.pushScope();
    for (const auto &p : s.params)
    {
        scopes_.bind(p.name, p.name);
        symbols_.insert(p.name);
        SemanticAnalyzer::Type vt = SemanticAnalyzer::Type::Int;
        if (p.type == ::il::frontends::basic::Type::Str)
            vt = SemanticAnalyzer::Type::String;
        else if (p.type == ::il::frontends::basic::Type::F64)
            vt = SemanticAnalyzer::Type::Float;
        else if (p.type == ::il::frontends::basic::Type::Bool)
            vt = SemanticAnalyzer::Type::Bool;
        varTypes_[p.name] = vt;
        if (p.is_array)
            arrays_[p.name] = -1;
    }
    for (const auto &st : s.body)
        if (st)
            visitStmt(*st);
    scopes_.popScope();
    symbols_ = std::move(symSave);
    varTypes_ = std::move(typeSave);
    arrays_ = std::move(arrSave);
    labels_ = std::move(labelSave);
    labelRefs_ = std::move(labelRefSave);
    forStack_ = std::move(forSave);
}

/// @brief Check whether a sequence of statements guarantees a return value.
///
/// The analysis is structural and conservative:
/// - `RETURN` with an expression returns true.
/// - `IF`/`ELSEIF`/`ELSE` returns only if all arms return.
/// - `WHILE` and `FOR` are treated as potentially non-terminating and thus do
///   not guarantee a return, regardless of their bodies.
/// - For a list of statements, only the last statement is considered.
bool SemanticAnalyzer::mustReturn(const std::vector<StmtPtr> &stmts) const
{
    if (stmts.empty())
        return false;
    return mustReturn(*stmts.back());
}

/// @brief Determine whether a single statement returns a value on all paths.
bool SemanticAnalyzer::mustReturn(const Stmt &s) const
{
    if (auto *lst = dynamic_cast<const StmtList *>(&s))
        return !lst->stmts.empty() && mustReturn(*lst->stmts.back());
    if (auto *ret = dynamic_cast<const ReturnStmt *>(&s))
        return ret->value != nullptr;
    if (auto *ifs = dynamic_cast<const IfStmt *>(&s))
    {
        if (!ifs->then_branch || !mustReturn(*ifs->then_branch))
            return false;
        for (const auto &e : ifs->elseifs)
            if (!e.then_branch || !mustReturn(*e.then_branch))
                return false;
        if (!ifs->else_branch)
            return false;
        return mustReturn(*ifs->else_branch);
    }
    if (dynamic_cast<const WhileStmt *>(&s) || dynamic_cast<const ForStmt *>(&s))
        return false;
    return false;
}

void SemanticAnalyzer::analyze(const Program &prog)
{
    symbols_.clear();
    labels_.clear();
    labelRefs_.clear();
    forStack_.clear();
    varTypes_.clear();
    arrays_.clear();
    procReg_.clear();
    scopes_.reset();

    for (const auto &p : prog.procs)
        if (p)
        {
            if (auto *f = dynamic_cast<FunctionDecl *>(p.get()))
                procReg_.registerProc(*f);
            else if (auto *s = dynamic_cast<SubDecl *>(p.get()))
                procReg_.registerProc(*s);
        }
    for (const auto &p : prog.procs)
        if (p)
        {
            if (auto *f = dynamic_cast<FunctionDecl *>(p.get()))
                analyzeProc(*f);
            else if (auto *s = dynamic_cast<SubDecl *>(p.get()))
                analyzeProc(*s);
        }
    for (const auto &stmt : prog.main)
        if (stmt)
            labels_.insert(stmt->line);
    for (const auto &stmt : prog.main)
        if (stmt)
            visitStmt(*stmt);
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
    if (auto mapped = scopes_.resolve(v.name))
        v.name = *mapped;
    symbols_.insert(v.name);
    Type varTy = Type::Int;
    auto itType = varTypes_.find(v.name);
    if (itType != varTypes_.end())
    {
        varTy = itType->second;
    }
    else if (!v.name.empty())
    {
        if (v.name.back() == '$')
            varTy = Type::String;
        else if (v.name.back() == '#')
            varTy = Type::Float;
    }
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
    if (itType == varTypes_.end())
        varTypes_[v.name] = varTy;
}

void SemanticAnalyzer::analyzeArrayAssignment(ArrayExpr &a, const LetStmt &l)
{
    if (auto mapped = scopes_.resolve(a.name))
        a.name = *mapped;
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

void SemanticAnalyzer::analyzeIf(const IfStmt &i)
{
    if (i.cond)
        visitExpr(*i.cond);
    if (i.then_branch)
    {
        ScopeTracker::ScopedScope scope(scopes_);
        visitStmt(*i.then_branch);
    }
    for (const auto &e : i.elseifs)
    {
        if (e.cond)
            visitExpr(*e.cond);
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
        visitExpr(*w.cond);
    ScopeTracker::ScopedScope scope(scopes_);
    for (const auto &bs : w.body)
        if (bs)
            visitStmt(*bs);
}

void SemanticAnalyzer::analyzeFor(const ForStmt &f)
{
    auto *fc = const_cast<ForStmt *>(&f);
    if (auto mapped = scopes_.resolve(fc->var))
        fc->var = *mapped;
    symbols_.insert(fc->var);
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
    if (auto mapped = scopes_.resolve(ic->var))
        ic->var = *mapped;
    symbols_.insert(ic->var);
    if (!ic->var.empty() && ic->var.back() == '$')
        varTypes_[ic->var] = Type::String;
    else if (!ic->var.empty() && ic->var.back() == '#')
        varTypes_[ic->var] = Type::Float;
    else
        varTypes_[ic->var] = Type::Int;
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

void SemanticAnalyzer::visitStmt(const Stmt &s)
{
    using Handler = void (*)(SemanticAnalyzer *, const Stmt &);
    static const std::unordered_map<std::type_index, Handler> table = {
        {typeid(StmtList), &dispatchHelper<StmtList, &SemanticAnalyzer::analyzeStmtList>},
        {typeid(PrintStmt), &dispatchHelper<PrintStmt, &SemanticAnalyzer::analyzePrint>},
        {typeid(LetStmt), &dispatchHelper<LetStmt, &SemanticAnalyzer::analyzeLet>},
        {typeid(IfStmt), &dispatchHelper<IfStmt, &SemanticAnalyzer::analyzeIf>},
        {typeid(WhileStmt), &dispatchHelper<WhileStmt, &SemanticAnalyzer::analyzeWhile>},
        {typeid(ForStmt), &dispatchHelper<ForStmt, &SemanticAnalyzer::analyzeFor>},
        {typeid(GotoStmt), &dispatchHelper<GotoStmt, &SemanticAnalyzer::analyzeGoto>},
        {typeid(NextStmt), &dispatchHelper<NextStmt, &SemanticAnalyzer::analyzeNext>},
        {typeid(EndStmt), &dispatchHelper<EndStmt, &SemanticAnalyzer::analyzeEnd>},
        {typeid(RandomizeStmt),
         &dispatchHelper<RandomizeStmt, &SemanticAnalyzer::analyzeRandomize>},
        {typeid(InputStmt), &dispatchHelper<InputStmt, &SemanticAnalyzer::analyzeInput>},
        {typeid(DimStmt), &dispatchHelper<DimStmt, &SemanticAnalyzer::analyzeDim>},
    };
    auto it = table.find(typeid(s));
    if (it != table.end())
        it->second(this, s);
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeVar(VarExpr &v)
{
    if (auto mapped = scopes_.resolve(v.name))
        v.name = *mapped;
    if (!symbols_.count(v.name))
    {
        std::string best;
        size_t bestDist = std::numeric_limits<size_t>::max();
        for (const auto &s : symbols_)
        {
            size_t d = levenshtein(v.name, s);
            if (d < bestDist)
            {
                bestDist = d;
                best = s;
            }
        }
        std::string msg = "unknown variable '" + v.name + "'";
        if (!best.empty())
            msg += "; did you mean '" + best + "'?";
        de.emit(il::support::Severity::Error,
                "B1001",
                v.loc,
                static_cast<uint32_t>(v.name.size()),
                std::move(msg));
        return Type::Unknown;
    }
    auto it = varTypes_.find(v.name);
    if (it != varTypes_.end())
        return it->second;
    if (!v.name.empty())
    {
        if (v.name.back() == '$')
            return Type::String;
        if (v.name.back() == '#')
            return Type::Float;
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeUnary(const UnaryExpr &u)
{
    Type t = Type::Unknown;
    if (u.expr)
        t = visitExpr(*u.expr);
    if (u.op == UnaryExpr::Op::LogicalNot)
    {
        if (t != Type::Unknown && t != Type::Bool)
        {
            std::ostringstream oss;
            oss << "NOT requires a BOOLEAN operand, got " << semanticTypeName(t) << '.';
            de.emit(il::support::Severity::Error,
                    std::string(DiagNonBooleanNotOperand),
                    u.loc,
                    3,
                    oss.str());
        }
        return Type::Bool;
    }
    return Type::Unknown;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeBinary(const BinaryExpr &b)
{
    Type lt = Type::Unknown;
    Type rt = Type::Unknown;
    if (b.lhs)
        lt = visitExpr(*b.lhs);
    if (b.rhs)
        rt = visitExpr(*b.rhs);
    switch (b.op)
    {
        case BinaryExpr::Op::Add:
        case BinaryExpr::Op::Sub:
        case BinaryExpr::Op::Mul:
            return analyzeArithmetic(b, lt, rt);
        case BinaryExpr::Op::Div:
        case BinaryExpr::Op::IDiv:
        case BinaryExpr::Op::Mod:
            return analyzeDivMod(b, lt, rt);
        case BinaryExpr::Op::Eq:
        case BinaryExpr::Op::Ne:
        case BinaryExpr::Op::Lt:
        case BinaryExpr::Op::Le:
        case BinaryExpr::Op::Gt:
        case BinaryExpr::Op::Ge:
            return analyzeComparison(b, lt, rt);
        case BinaryExpr::Op::LogicalAndShort:
        case BinaryExpr::Op::LogicalOrShort:
        case BinaryExpr::Op::LogicalAnd:
        case BinaryExpr::Op::LogicalOr:
            return analyzeLogical(b, lt, rt);
    }
    return Type::Unknown;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeArithmetic(const BinaryExpr &b, Type lt, Type rt)
{
    auto isNum = [](Type t) { return t == Type::Int || t == Type::Float || t == Type::Unknown; };
    if (!isNum(lt) || !isNum(rt))
    {
        std::string msg = "operand type mismatch";
        de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
    }
    return (lt == Type::Float || rt == Type::Float) ? Type::Float : Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeDivMod(const BinaryExpr &b, Type lt, Type rt)
{
    auto isNum = [](Type t) { return t == Type::Int || t == Type::Float || t == Type::Unknown; };
    switch (b.op)
    {
        case BinaryExpr::Op::Div:
        {
            if (!isNum(lt) || !isNum(rt))
            {
                std::string msg = "operand type mismatch";
                de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
            }
            if (lt == Type::Float || rt == Type::Float)
                return Type::Float;
            if (dynamic_cast<const IntExpr *>(b.lhs.get()) &&
                dynamic_cast<const IntExpr *>(b.rhs.get()))
            {
                auto *ri = static_cast<const IntExpr *>(b.rhs.get());
                if (ri->value == 0)
                {
                    std::string msg = "divide by zero";
                    de.emit(il::support::Severity::Error, "B2002", b.loc, 1, std::move(msg));
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
                de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
            }
            if (dynamic_cast<const IntExpr *>(b.lhs.get()) &&
                dynamic_cast<const IntExpr *>(b.rhs.get()))
            {
                auto *ri = static_cast<const IntExpr *>(b.rhs.get());
                if (ri->value == 0)
                {
                    std::string msg = "divide by zero";
                    de.emit(il::support::Severity::Error, "B2002", b.loc, 1, std::move(msg));
                }
            }
            return Type::Int;
        }
        default:
            break;
    }
    return Type::Unknown;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeComparison(const BinaryExpr &b, Type lt, Type rt)
{
    auto isNum = [](Type t) { return t == Type::Int || t == Type::Float || t == Type::Unknown; };
    auto isStr = [](Type t) { return t == Type::String || t == Type::Unknown; };

    const bool numeric_ok = isNum(lt) && isNum(rt);
    const bool string_ok =
        isStr(lt) && isStr(rt) && (b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne);

    if (string_ok)
        return Type::Bool;

    if (!numeric_ok)
    {
        std::string msg = "operand type mismatch";
        de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
        return Type::Bool;
    }

    return Type::Bool;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLogical(const BinaryExpr &b, Type lt, Type rt)
{
    auto isBool = [](Type t) { return t == Type::Unknown || t == Type::Bool; };
    if (!isBool(lt) || !isBool(rt))
    {
        std::ostringstream oss;
        oss << "Logical operator " << logicalOpName(b.op)
            << " requires BOOLEAN operands, got " << semanticTypeName(lt) << " and "
            << semanticTypeName(rt) << '.';
        de.emit(il::support::Severity::Error,
                std::string(DiagNonBooleanLogicalOperand),
                b.loc,
                1,
                oss.str());
    }
    return Type::Bool;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeBuiltinCall(const BuiltinCallExpr &c)
{
    std::vector<Type> argTys;
    for (auto &a : c.args)
        argTys.push_back(a ? visitExpr(*a) : Type::Unknown);
    const auto &info = getBuiltinInfo(c.builtin);
    if (info.analyze)
        return (this->*(info.analyze))(c, argTys);
    return Type::Unknown;
}

bool SemanticAnalyzer::checkArgCount(const BuiltinCallExpr &c,
                                     const std::vector<Type> &args,
                                     size_t min,
                                     size_t max)
{
    if (args.size() < min || args.size() > max)
    {
        std::ostringstream oss;
        oss << builtinName(c.builtin) << ": expected ";
        if (min == max)
            oss << min << " arg" << (min == 1 ? "" : "s");
        else
            oss << min << '-' << max << " args";
        oss << " (got " << args.size() << ')';
        de.emit(il::support::Severity::Error, "B2001", c.loc, 1, oss.str());
        return false;
    }
    return true;
}

bool SemanticAnalyzer::checkArgType(const BuiltinCallExpr &c,
                                    size_t idx,
                                    Type argTy,
                                    std::initializer_list<Type> allowed)
{
    if (argTy == Type::Unknown)
        return true;
    for (Type t : allowed)
        if (t == argTy)
            return true;
    il::support::SourceLoc loc = (idx < c.args.size() && c.args[idx]) ? c.args[idx]->loc : c.loc;
    bool wantString = false;
    bool wantNumber = false;
    for (Type t : allowed)
    {
        if (t == Type::String)
            wantString = true;
        if (t == Type::Int || t == Type::Float)
            wantNumber = true;
    }
    const char *need = wantString ? (wantNumber ? "value" : "string") : "number";
    const char *got = "unknown";
    if (argTy == Type::String)
        got = "string";
    else if (argTy == Type::Int || argTy == Type::Float)
        got = "number";
    std::ostringstream oss;
    oss << builtinName(c.builtin) << ": arg " << (idx + 1) << " must be " << need << " (got " << got
        << ')';
    de.emit(il::support::Severity::Error, "B2001", loc, 1, oss.str());
    return false;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeRnd(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    checkArgCount(c, args, 0, 0);
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLen(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeMid(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 3))
    {
        checkArgType(c, 0, args[0], {Type::String});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
        if (args.size() == 3)
            checkArgType(c, 2, args[2], {Type::Int, Type::Float});
    }
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLeft(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 2))
    {
        checkArgType(c, 0, args[0], {Type::String});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
    }
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeRight(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 2))
    {
        checkArgType(c, 0, args[0], {Type::String});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
    }
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeStr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeVal(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeInt(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Float});
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeInstr(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 3))
    {
        size_t idx = 0;
        if (args.size() == 3)
        {
            checkArgType(c, idx, args[idx], {Type::Int, Type::Float});
            idx++;
        }
        checkArgType(c, idx, args[idx], {Type::String});
        idx++;
        checkArgType(c, idx, args[idx], {Type::String});
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLtrim(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeRtrim(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeTrim(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeUcase(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLcase(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeChr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeAsc(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeSqr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeAbs(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1) && !args.empty())
    {
        if (args[0] == Type::Float)
            return Type::Float;
        if (args[0] == Type::Int || args[0] == Type::Unknown)
            return Type::Int;
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeFloor(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeCeil(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeSin(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeCos(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzePow(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 2))
    {
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
    }
    return Type::Float;
}

const ProcSignature *SemanticAnalyzer::resolveCallee(const CallExpr &c)
{
    const ProcSignature *sig = procReg_.lookup(c.callee);
    if (!sig)
    {
        std::string msg = "unknown procedure '" + c.callee + "'";
        de.emit(il::support::Severity::Error,
                "B1006",
                c.loc,
                static_cast<uint32_t>(c.callee.size()),
                std::move(msg));
        return nullptr;
    }
    if (sig->kind == ProcSignature::Kind::Sub)
    {
        std::string msg = "subroutine '" + c.callee + "' used in expression";
        de.emit(il::support::Severity::Error,
                "B2005",
                c.loc,
                static_cast<uint32_t>(c.callee.size()),
                std::move(msg));
        return nullptr;
    }
    return sig;
}

std::vector<SemanticAnalyzer::Type> SemanticAnalyzer::checkCallArgs(const CallExpr &c,
                                                                    const ProcSignature *sig)
{
    std::vector<Type> argTys;
    for (auto &a : c.args)
        argTys.push_back(a ? visitExpr(*a) : Type::Unknown);

    if (!sig)
        return argTys;

    if (argTys.size() != sig->params.size())
    {
        std::string msg = "wrong number of arguments";
        de.emit(il::support::Severity::Error, "B2005", c.loc, 1, std::move(msg));
    }

    size_t n = std::min(argTys.size(), sig->params.size());
    for (size_t i = 0; i < n; ++i)
    {
        auto expectTy = sig->params[i].type;
        auto argTy = argTys[i];
        if (sig->params[i].is_array)
        {
            auto *argExpr = c.args[i].get();
            auto *v = dynamic_cast<VarExpr *>(argExpr);
            if (!v || !arrays_.count(v->name))
            {
                il::support::SourceLoc loc = argExpr ? argExpr->loc : c.loc;
                std::string msg = "argument " + std::to_string(i + 1) + " to " + c.callee +
                                  " must be an array variable (ByRef)";
                de.emit(il::support::Severity::Error, "B2006", loc, 1, std::move(msg));
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
        else if (expectTy == ::il::frontends::basic::Type::Bool)
            want = Type::Bool;
        if (argTy != Type::Unknown && argTy != want)
        {
            std::string msg = "argument type mismatch";
            de.emit(il::support::Severity::Error, "B2001", c.loc, 1, std::move(msg));
        }
    }
    return argTys;
}

SemanticAnalyzer::Type SemanticAnalyzer::inferCallType([[maybe_unused]] const CallExpr &c,
                                                       const ProcSignature *sig)
{
    if (!sig || !sig->retType)
        return Type::Unknown;
    if (*sig->retType == ::il::frontends::basic::Type::F64)
        return Type::Float;
    if (*sig->retType == ::il::frontends::basic::Type::Str)
        return Type::String;
    if (*sig->retType == ::il::frontends::basic::Type::Bool)
        return Type::Bool;
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeCall(const CallExpr &c)
{
    const ProcSignature *sig = resolveCallee(c);
    auto argTys [[maybe_unused]] = checkCallArgs(c, sig);
    return inferCallType(c, sig);
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeArray(ArrayExpr &a)
{
    if (auto mapped = scopes_.resolve(a.name))
        a.name = *mapped;
    if (!arrays_.count(a.name))
    {
        std::string msg = "unknown array '" + a.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::move(msg));
        visitExpr(*a.index);
        return Type::Unknown;
    }
    Type ty = visitExpr(*a.index);
    if (ty != Type::Unknown && ty != Type::Int)
    {
        std::string msg = "index type mismatch";
        de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
    }
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
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::visitExpr(const Expr &e)
{
    if (dynamic_cast<const IntExpr *>(&e))
        return Type::Int;
    if (dynamic_cast<const FloatExpr *>(&e))
        return Type::Float;
    if (dynamic_cast<const StringExpr *>(&e))
        return Type::String;
    if (dynamic_cast<const BoolExpr *>(&e))
        return Type::Bool;
    if (auto *v = const_cast<VarExpr *>(dynamic_cast<const VarExpr *>(&e)))
        return analyzeVar(*v);
    if (auto *u = dynamic_cast<const UnaryExpr *>(&e))
        return analyzeUnary(*u);
    if (auto *b = dynamic_cast<const BinaryExpr *>(&e))
        return analyzeBinary(*b);
    if (auto *bc = dynamic_cast<const BuiltinCallExpr *>(&e))
        return analyzeBuiltinCall(*bc);
    if (auto *c = dynamic_cast<const CallExpr *>(&e))
        return analyzeCall(*c);
    if (auto *a = const_cast<ArrayExpr *>(dynamic_cast<const ArrayExpr *>(&e)))
        return analyzeArray(*a);
    return Type::Unknown;
}

} // namespace il::frontends::basic
