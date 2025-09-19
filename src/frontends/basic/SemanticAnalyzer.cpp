// File: src/frontends/basic/SemanticAnalyzer.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
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
#include <utility>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

namespace
{
/// @brief Invoke a member handler for a concrete statement type.
/// @tparam T Statement subclass expected by the handler.
/// @tparam Fn Member function pointer that processes @p T.
/// @param sa Analyzer performing the dispatch.
/// @param s Statement instance supplied by the visitor.
/// @note Dispatching itself has no diagnostics; the target member issues them.
template <typename T, void (SemanticAnalyzer::*Fn)(const T &)>
void dispatchHelper(SemanticAnalyzer *sa, const Stmt &s)
{
    (sa->*Fn)(static_cast<const T &>(s));
}

/// @brief Compute the Levenshtein edit distance between two strings.
/// @param a Candidate identifier from user input.
/// @param b Known symbol to compare against.
/// @return Minimum number of edits to transform @p a into @p b.
/// @details Implements the dynamic-programming algorithm with rolling buffers
///          to keep memory usage linear in the shorter dimension.
/// @note Used when suggesting likely symbol names in diagnostics.
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

/// @brief Convert an AST-level BASIC type to the analyzer's internal enum.
/// @param ty AST type recorded on declarations.
/// @return Semantic analyzer representation of @p ty.
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
/// @param b Builtin enumerator to describe.
/// @return BASIC keyword spelling for @p b.
static const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

/// @brief Produce a human-readable name for a semantic type.
/// @param type Inferred BASIC type to describe.
/// @return Canonical uppercase BASIC type label.
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

/// @brief Translate a logical operator into its BASIC keyword spelling.
/// @param op Logical binary operator.
/// @return Keyword string used in diagnostics.
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

/// @brief Produce a short textual description of @p expr for diagnostics.
/// @param expr Expression appearing within a conditional context.
/// @return Human-readable description used to clarify diagnostics.
/// @note Prefers literal renderings and variable names to keep messages concise.
static std::string conditionExprText(const Expr &expr)
{
    if (auto *var = dynamic_cast<const VarExpr *>(&expr))
        return var->name;
    if (auto *intExpr = dynamic_cast<const IntExpr *>(&expr))
        return std::to_string(intExpr->value);
    if (auto *floatExpr = dynamic_cast<const FloatExpr *>(&expr))
    {
        std::ostringstream oss;
        oss << floatExpr->value;
        return oss.str();
    }
    if (auto *boolExpr = dynamic_cast<const BoolExpr *>(&expr))
        return boolExpr->value ? "TRUE" : "FALSE";
    if (auto *strExpr = dynamic_cast<const StringExpr *>(&expr))
    {
        std::string text = "\"";
        text += strExpr->value;
        text += '"';
        return text;
    }
    return {};
}

} // namespace

/// @brief Analyze a procedure declaration shared logic between functions/subs.
/// @tparam Proc Procedure AST type (FunctionDecl or SubDecl).
/// @tparam BodyCallback Callable invoked after traversing the body.
/// @param proc Procedure to analyze.
/// @param bodyCheck Callback responsible for return-specific diagnostics.
/// @note Binds parameters into a fresh scope, visits the body to infer symbol
///       usage, and allows @p bodyCheck to emit diagnostics such as missing
///       returns.
template <typename Proc, typename BodyCallback>
void SemanticAnalyzer::analyzeProcedureCommon(const Proc &proc, BodyCallback &&bodyCheck)
{
    ProcedureScope procScope(*this);
    for (const auto &p : proc.params)
    {
        scopes_.bind(p.name, p.name);
        symbols_.insert(p.name);
        varTypes_[p.name] = astToSemanticType(p.type);
        if (p.is_array)
            arrays_[p.name] = -1;
    }
    for (const auto &st : proc.body)
        if (st)
            visitStmt(*st);

    std::forward<BodyCallback>(bodyCheck)(proc);
}

/// @brief Analyze a BASIC FUNCTION declaration.
/// @param f Function AST node providing signature and body.
/// @note Emits diagnostics when a function body lacks a guaranteed RETURN or
///       when nested statements report issues via visit callbacks.
void SemanticAnalyzer::analyzeProc(const FunctionDecl &f)
{
    analyzeProcedureCommon(f, [this](const FunctionDecl &func) {
        if (mustReturn(func.body))
            return;

        std::string msg = "missing return in FUNCTION " + func.name;
        de.emit(il::support::Severity::Error,
                "B1007",
                func.endLoc.isValid() ? func.endLoc : func.loc,
                3,
                std::move(msg));
    });
}

/// @brief Analyze a BASIC SUB declaration.
/// @param s Subroutine AST node.
/// @note Traverses the body to populate scopes and forward diagnostics from
///       statement analysis.
void SemanticAnalyzer::analyzeProc(const SubDecl &s)
{
    analyzeProcedureCommon(s, [](const SubDecl &) {});
}

/// @brief Check whether a sequence of statements guarantees a return value.
/// @param stmts Statements belonging to a procedure body.
/// @return True when the tail of @p stmts returns on all paths.
///
/// The analysis is structural and conservative:
/// - `RETURN` with an expression returns true.
/// - `IF`/`ELSEIF`/`ELSE` returns only if all arms return.
/// - `WHILE` and `FOR` are treated as potentially non-terminating and thus do
///   not guarantee a return, regardless of their bodies.
/// - For a list of statements, only the last statement is considered.
/// @note Pure analysis helper that does not emit diagnostics.
bool SemanticAnalyzer::mustReturn(const std::vector<StmtPtr> &stmts) const
{
    if (stmts.empty())
        return false;
    return mustReturn(*stmts.back());
}

/// @brief Determine whether a single statement returns a value on all paths.
/// @param s Statement to inspect.
/// @return True if @p s guarantees a return value.
/// @note Recurses into structured statements without emitting diagnostics.
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

/// @brief Analyze an entire BASIC program.
/// @param prog Program node containing procedures and main statements.
/// @note Clears previous state, registers procedures, and visits each
///       statement so nested handlers can emit diagnostics for symbol issues,
///       flow errors, and type mismatches.
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

/// @brief Analyze each statement in a statement list.
/// @param lst Statement list container.
/// @note Delegates to visitStmt so diagnostics originate from specific
///       statement handlers.
void SemanticAnalyzer::analyzeStmtList(const StmtList &lst)
{
    for (const auto &st : lst.stmts)
        if (st)
            visitStmt(*st);
}

/// @brief Validate expressions contained in a PRINT statement.
/// @param p PRINT statement AST node.
/// @note Emits diagnostics indirectly through expression visitors when
///       encountering invalid operands.
void SemanticAnalyzer::analyzePrint(const PrintStmt &p)
{
    for (const auto &it : p.items)
        if (it.kind == PrintItem::Kind::Expr && it.expr)
            visitExpr(*it.expr);
}

/// @brief Validate an assignment to a scalar variable.
/// @param v Variable expression being assigned to.
/// @param l LET statement providing the assignment context.
/// @note Resolves scoped bindings, infers variable type, and emits operand
///       mismatch diagnostics (B2001) when the assigned expression is
///       incompatible.
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

/// @brief Validate an assignment to an array element.
/// @param a Array element target.
/// @param l LET statement providing assigned expression.
/// @note Confirms the array exists, validates index type and bounds, and emits
///       diagnostics such as unknown arrays (B1001), type mismatches (B2001),
///       and bounds warnings (B3001).
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

/// @brief Report an error for LET targets that are not assignable.
/// @param l LET statement with an invalid left-hand side.
/// @note Always emits diagnostic B2007 after visiting both sides to surface
///       additional issues in nested expressions.
void SemanticAnalyzer::analyzeConstExpr(const LetStmt &l)
{
    if (l.target)
        visitExpr(*l.target);
    if (l.expr)
        visitExpr(*l.expr);
    std::string msg = "left-hand side of LET must be a variable or array element";
    de.emit(il::support::Severity::Error, "B2007", l.loc, 1, std::move(msg));
}

/// @brief Analyze a LET assignment statement.
/// @param l LET statement AST node.
/// @note Dispatches to scalar/array helpers which may emit diagnostics for
///       unknown variables, type mismatches, or invalid targets.
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

/// @brief Ensure an expression used in a condition is boolean-compatible.
/// @param expr Expression appearing in control-flow predicate.
/// @note Emits diagnostic DiagNonBooleanCondition when the expression cannot
///       be interpreted as a boolean value.
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

/// @brief Analyze an IF/ELSEIF/ELSE statement chain.
/// @param i IF statement AST node.
/// @note Validates conditions, manages block scopes for branches, and relays
///       diagnostics from contained statements.
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

/// @brief Analyze a WHILE loop.
/// @param w WHILE statement AST node.
/// @note Checks the loop condition for boolean compatibility and visits the
///       loop body within a scoped environment, propagating diagnostics.
void SemanticAnalyzer::analyzeWhile(const WhileStmt &w)
{
    if (w.cond)
        checkConditionExpr(*w.cond);
    ScopeTracker::ScopedScope scope(scopes_);
    for (const auto &bs : w.body)
        if (bs)
            visitStmt(*bs);
}

/// @brief Analyze a FOR loop.
/// @param f FOR statement AST node.
/// @note Binds the loop variable, visits range expressions, tracks active loops
///       for NEXT validation, and surfaces diagnostics from expression and body
///       analysis.
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

/// @brief Analyze a GOTO statement.
/// @param g GOTO statement AST node.
/// @note Records referenced labels and emits diagnostic B1003 when the target
///       line is unknown.
void SemanticAnalyzer::analyzeGoto(const GotoStmt &g)
{
    labelRefs_.insert(g.target);
    if (!labels_.count(g.target))
    {
        std::string msg = "unknown line " + std::to_string(g.target);
        de.emit(il::support::Severity::Error, "B1003", g.loc, 4, std::move(msg));
    }
}

/// @brief Analyze a NEXT statement.
/// @param n NEXT statement AST node.
/// @note Validates correspondence with the active FOR stack, emitting
///       diagnostic B1002 when mismatched.
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

/// @brief Analyze an END statement.
/// @note END has no semantic checks; provided for completeness.
void SemanticAnalyzer::analyzeEnd(const EndStmt &)
{
    // nothing
}

/// @brief Analyze a RANDOMIZE statement.
/// @param r RANDOMIZE statement AST node.
/// @note Validates optional seed expression type, emitting diagnostic B2001 on
///       mismatch.
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

/// @brief Analyze an INPUT statement.
/// @param inp INPUT statement AST node.
/// @note Validates optional prompt expression, binds the input variable in the
///       current scope, and records inferred type for later use.
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

/// @brief Analyze a DIM declaration statement.
/// @param d DIM statement AST node.
/// @note Checks array bounds and sizes, enforces local uniqueness, and emits
///       diagnostics including B2001, B2003, and B1006 for invalid declarations.
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

/// @brief Dispatch a statement node to the appropriate analyzer.
/// @param s Statement to visit.
/// @note Uses a type-indexed dispatch table; diagnostics are emitted by the
///       specific handler invoked.
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

/// @brief Analyze a variable reference expression.
/// @param v Variable expression to resolve.
/// @return Inferred BASIC type for @p v.
/// @note Resolves scoped aliases, records symbols, and emits B1001 with
///       Levenshtein suggestions when the variable is unknown.
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

/// @brief Analyze a unary expression.
/// @param u Unary expression AST node.
/// @return Inferred BASIC type of the expression.
/// @note Emits diagnostic DiagNonBooleanNotOperand when a logical NOT operand
///       is not boolean-compatible.
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

/// @brief Analyze a binary expression and infer its type.
/// @param b Binary expression AST node.
/// @return Type resulting from applying @p b's operator to its operands.
/// @note Delegates to specialized helpers that emit diagnostics for type
///       mismatches, divide-by-zero, and logical misuse.
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

/// @brief Analyze arithmetic operators (+, -, *).
/// @param b Binary expression metadata (location/operator).
/// @param lt Inferred type of the left operand.
/// @param rt Inferred type of the right operand.
/// @return Resulting BASIC type after numeric promotion.
/// @note Emits diagnostic B2001 when either operand is not numeric.
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

/// @brief Analyze division and modulo operators.
/// @param b Binary expression metadata (location/operator).
/// @param lt Type of left operand.
/// @param rt Type of right operand.
/// @return Resulting BASIC type (integer or float).
/// @note Emits B2001 for type mismatches and B2002 for literal divide-by-zero
///       cases detected at analysis time.
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

/// @brief Analyze comparison operators.
/// @param b Binary expression metadata (location/operator).
/// @param lt Type of left operand.
/// @param rt Type of right operand.
/// @return Boolean result type regardless of operand types.
/// @note Emits diagnostic B2001 when operands are incompatible (e.g., mixing
///       strings with ordering comparisons).
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

/// @brief Analyze logical operators (AND/OR/short-circuit variants).
/// @param b Binary expression metadata (location/operator).
/// @param lt Type of left operand.
/// @param rt Type of right operand.
/// @return Boolean type for logical expressions.
/// @note Emits diagnostic DiagNonBooleanLogicalOperand when operands are not
///       boolean-compatible.
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

/// @brief Analyze a builtin function call expression.
/// @param c Builtin call AST node.
/// @return Inferred BASIC type based on builtin semantics.
/// @note Visits argument expressions, then delegates to builtin-specific
///       analyzers which emit diagnostics for incorrect usage.
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

/// @brief Validate builtin argument count against an expected range.
/// @param c Builtin call being checked.
/// @param args Types of evaluated arguments.
/// @param min Minimum allowed argument count.
/// @param max Maximum allowed argument count.
/// @return True when the call satisfies the arity constraint.
/// @note Emits diagnostic B2001 describing the mismatch when violated.
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

/// @brief Validate a single builtin argument type against allowed options.
/// @param c Builtin call containing the argument.
/// @param idx Zero-based argument index.
/// @param argTy Inferred type for the argument.
/// @param allowed Set of permissible types.
/// @return True when @p argTy is among @p allowed or unknown.
/// @note Emits diagnostic B2001 describing the expected kind when violated.
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

/// @brief Analyze the RND builtin.
/// @param c RND call expression.
/// @param args Argument types provided to RND.
/// @return Always returns Type::Float.
/// @note Relies on checkArgCount to emit B2001 if arguments are present.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeRnd(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    checkArgCount(c, args, 0, 0);
    return Type::Float;
}

/// @brief Analyze the LEN builtin.
/// @param c LEN call expression.
/// @param args Argument types supplied to LEN.
/// @return String length as Type::Int.
/// @note Emits B2001 through helpers when argument count or type is invalid.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeLen(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

/// @brief Analyze the MID$ builtin.
/// @param c MID call expression.
/// @param args Argument types supplied to MID.
/// @return Resulting substring type (Type::String).
/// @note Validates string argument followed by numeric offsets, emitting B2001
///       on misuse.
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

/// @brief Analyze the LEFT$ builtin.
/// @param c LEFT call expression.
/// @param args Argument types supplied to LEFT.
/// @return Result string type (Type::String).
/// @note Ensures a string source and numeric length, reporting B2001 when
///       violated.
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

/// @brief Analyze the RIGHT$ builtin.
/// @param c RIGHT call expression.
/// @param args Argument types supplied to RIGHT.
/// @return Result string type (Type::String).
/// @note Requires string input and numeric count; emits B2001 via helpers.
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

/// @brief Analyze the STR$ builtin.
/// @param c STR call expression.
/// @param args Argument types supplied to STR.
/// @return Result string type (Type::String).
/// @note Requires numeric argument, reporting B2001 via helper checks.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeStr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::String;
}

/// @brief Analyze the VAL builtin.
/// @param c VAL call expression.
/// @param args Argument types supplied to VAL.
/// @return Numeric result type (Type::Int).
/// @note Requires a string argument; reports B2001 when violated.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeVal(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

/// @brief Analyze the INT builtin.
/// @param c INT call expression.
/// @param args Argument types supplied to INT.
/// @return Truncated integer result type (Type::Int).
/// @note Requires a float argument, reporting B2001 for other types.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeInt(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Float});
    return Type::Int;
}

/// @brief Analyze the INSTR builtin.
/// @param c INSTR call expression.
/// @param args Argument types supplied to INSTR.
/// @return Integer search result (Type::Int).
/// @note Validates optional starting index and string operands, emitting B2001
///       on mismatch.
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

/// @brief Analyze the LTRIM$ builtin.
/// @param c LTRIM call expression.
/// @param args Argument types supplied to LTRIM.
/// @return Trimmed string type (Type::String).
/// @note Ensures a single string argument, emitting B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeLtrim(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

/// @brief Analyze the RTRIM$ builtin.
/// @param c RTRIM call expression.
/// @param args Argument types supplied to RTRIM.
/// @return Trimmed string type (Type::String).
/// @note Ensures a single string argument, emitting B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeRtrim(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

/// @brief Analyze the TRIM$ builtin.
/// @param c TRIM call expression.
/// @param args Argument types supplied to TRIM.
/// @return Trimmed string type (Type::String).
/// @note Ensures a single string argument, emitting B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeTrim(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

/// @brief Analyze the UCASE$ builtin.
/// @param c UCASE call expression.
/// @param args Argument types supplied to UCASE.
/// @return Upper-cased string (Type::String).
/// @note Requires one string argument; emits B2001 on mismatch.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeUcase(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

/// @brief Analyze the LCASE$ builtin.
/// @param c LCASE call expression.
/// @param args Argument types supplied to LCASE.
/// @return Lower-cased string (Type::String).
/// @note Requires one string argument; emits B2001 on mismatch.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeLcase(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

/// @brief Analyze the CHR$ builtin.
/// @param c CHR call expression.
/// @param args Argument types supplied to CHR.
/// @return Single-character string result (Type::String).
/// @note Requires numeric argument; emits B2001 via helper checks.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeChr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::String;
}

/// @brief Analyze the ASC builtin.
/// @param c ASC call expression.
/// @param args Argument types supplied to ASC.
/// @return Character code result (Type::Int).
/// @note Requires string argument; emits B2001 for other types.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeAsc(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

/// @brief Analyze the SQR builtin.
/// @param c SQR call expression.
/// @param args Argument types supplied to SQR.
/// @return Floating result type (Type::Float).
/// @note Requires numeric argument; emits B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeSqr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

/// @brief Analyze the ABS builtin.
/// @param c ABS call expression.
/// @param args Argument types supplied to ABS.
/// @return Numeric result mirroring argument precision.
/// @note Emits B2001 when the argument is not numeric.
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

/// @brief Analyze the FLOOR builtin.
/// @param c FLOOR call expression.
/// @param args Argument types supplied to FLOOR.
/// @return Floating result type (Type::Float).
/// @note Requires numeric argument; emits B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeFloor(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

/// @brief Analyze the CEIL builtin.
/// @param c CEIL call expression.
/// @param args Argument types supplied to CEIL.
/// @return Floating result type (Type::Float).
/// @note Requires numeric argument; emits B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeCeil(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

/// @brief Analyze the SIN builtin.
/// @param c SIN call expression.
/// @param args Argument types supplied to SIN.
/// @return Floating result type (Type::Float).
/// @note Requires numeric argument; emits B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeSin(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

/// @brief Analyze the COS builtin.
/// @param c COS call expression.
/// @param args Argument types supplied to COS.
/// @return Floating result type (Type::Float).
/// @note Requires numeric argument; emits B2001 otherwise.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeCos(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

/// @brief Analyze the POW builtin.
/// @param c POW call expression.
/// @param args Argument types supplied to POW.
/// @return Floating result type (Type::Float).
/// @note Requires two numeric arguments; emits B2001 otherwise.
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

/// @brief Resolve a user-defined call to a registered procedure.
/// @param c Call expression being analyzed.
/// @return Procedure signature when found, otherwise nullptr.
/// @note Emits B1006 for unknown procedures and B2005 when a SUB is used in an
///       expression context.
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

/// @brief Validate argument types against a procedure signature.
/// @param c Call expression providing arguments.
/// @param sig Signature describing expected parameter types (may be null).
/// @return Vector of inferred argument types.
/// @note Emits diagnostics for wrong arity (B2005), missing array references
///       (B2006), and general type mismatches (B2001).
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

/// @brief Infer the return type of a call from its signature.
/// @param c Call expression (unused when signature is known).
/// @param sig Signature describing the callee.
/// @return Semantic type corresponding to the signature's return type.
/// @note Does not emit diagnostics; callers handle unknown signatures.
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

/// @brief Analyze a call to a user-defined function.
/// @param c Call expression AST node.
/// @return Inferred result type of the call.
/// @note Combines callee resolution, argument validation, and return type
///       inference; diagnostics are produced by helper routines.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeCall(const CallExpr &c)
{
    const ProcSignature *sig = resolveCallee(c);
    auto argTys [[maybe_unused]] = checkCallArgs(c, sig);
    return inferCallType(c, sig);
}

/// @brief Analyze an array element expression.
/// @param a Array expression to validate.
/// @return Resulting element type (Type::Int by default).
/// @note Emits diagnostics for unknown arrays (B1001), non-integer indices
///       (B2001), and static bounds warnings (B3001).
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

/// @brief Dispatch expression analysis based on runtime type.
/// @param e Expression AST node.
/// @return Inferred BASIC type for the expression.
/// @note Forwards diagnostics through specific analyzers; returns Type::Unknown
///       when a node type is not recognized.
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
