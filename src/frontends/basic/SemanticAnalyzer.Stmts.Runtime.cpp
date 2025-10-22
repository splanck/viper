//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer.Stmts.Runtime.cpp
// Purpose: Implement runtime-oriented BASIC statement checks (LET/DIM/REDIM,
//          RANDOMIZE, CALL, etc.).
// Key invariants: Loop variable protection and array/type tracking stay
//                 consistent with procedure scopes.
// Ownership/Lifetime: Borrowed SemanticAnalyzer state only.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime statement analysis helpers for the BASIC semantic analyzer.
/// @details Provides `RuntimeStmtContext` and the semantic analyzer methods that
///          enforce typing and lifetime rules for runtime-facing statements such
///          as LET, DIM, REDIM, RANDOMIZE, and CALL.  The functions here
///          coordinate with symbol tracking to keep procedure-scoped metadata in
///          sync.

#include "frontends/basic/SemanticAnalyzer.Stmts.Runtime.hpp"

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Construct a runtime statement context tied to the active analyzer.
/// @details The context inherits from @ref StmtShared to share convenience
///          utilities (loop tracking, diagnostics) with other statement
///          categories.  It simply binds the analyzer reference so helper
///          methods can query and mutate shared state during runtime checks.
/// @param analyzer Semantic analyzer orchestrating the current visit.
RuntimeStmtContext::RuntimeStmtContext(SemanticAnalyzer &analyzer) noexcept
    : StmtShared(analyzer)
{
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::RuntimeStmtContext;
using semantic_analyzer_detail::astToSemanticType;
using semantic_analyzer_detail::semanticTypeName;

/// @brief Validate a CALL statement that invokes a subroutine.
/// @details Ensures the callee resolves to a known SUB and checks each argument
///          against the procedure signature.  Diagnostics are emitted when the
///          target is missing or arguments mismatch.
/// @param stmt CALL statement AST node.
void SemanticAnalyzer::analyzeCallStmt(CallStmt &stmt)
{
    if (!stmt.call)
        return;
    const ProcSignature *sig = resolveCallee(*stmt.call, ProcSignature::Kind::Sub);
    checkCallArgs(*stmt.call, sig);
}

/// @brief Analyse assignment into a scalar variable.
/// @details Resolves the variable's canonical name, protects loop-control
///          variables from mutation, and enforces type compatibility between the
///          destination and expression.  Float-to-int promotions optionally
///          widen the tracked type when safe; otherwise the conversion is
///          flagged.
/// @param v Variable expression receiving the assignment.
/// @param l LET statement containing the assignment.
void SemanticAnalyzer::analyzeVarAssignment(VarExpr &v, const LetStmt &l)
{
    RuntimeStmtContext ctx(*this);
    resolveAndTrackSymbol(v.name, SymbolKind::Definition);
    if (ctx.isLoopVariable(v.name))
        ctx.reportLoopVariableMutation(v.name, l.loc, static_cast<uint32_t>(v.name.size()));

    Type varTy = Type::Int;
    if (auto itType = varTypes_.find(v.name); itType != varTypes_.end())
        varTy = itType->second;
    Type exprTy = Type::Unknown;
    if (l.expr)
        exprTy = visitExpr(*l.expr);

    if (varTy == Type::ArrayInt)
    {
        if (exprTy != Type::Unknown && exprTy != Type::ArrayInt)
        {
            std::string msg = "cannot assign scalar to array variable";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
        return;
    }

    if (!l.expr)
        return;

    if (exprTy == Type::ArrayInt)
    {
        std::string msg = "cannot assign array value to scalar variable";
        de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
    }
    else if (varTy == Type::Int && exprTy == Type::Float)
    {
        bool allowFloatPromotion = false;
        if (l.expr)
        {
            if (const auto *bin = dynamic_cast<const BinaryExpr *>(l.expr.get()))
            {
                const bool hasExplicitIntSuffix = !v.name.empty() && (v.name.back() == '%' || v.name.back() == '&');
                switch (bin->op)
                {
                    case BinaryExpr::Op::Div:
                    case BinaryExpr::Op::Add:
                    case BinaryExpr::Op::Sub:
                    case BinaryExpr::Op::Mul:
                        allowFloatPromotion = !hasExplicitIntSuffix;
                        break;
                    default:
                        break;
                }
            }
        }

        if (allowFloatPromotion)
        {
            if (activeProcScope_)
            {
                std::optional<Type> previous = varTy;
                activeProcScope_->noteVarTypeMutation(v.name, previous);
            }
            varTypes_[v.name] = Type::Float;
        }
        else
        {
            markImplicitConversion(*l.expr, Type::Int);
            std::string msg = "narrowing conversion from FLOAT to INT in assignment";
            de.emit(il::support::Severity::Warning, "B2002", l.loc, 1, std::move(msg));
        }
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

/// @brief Analyse assignment into an array element.
/// @details Validates that the array exists, the target really is an array, the
///          index evaluates to an integer, and the value type matches the array
///          element type.  Literal indices are additionally range checked when
///          bounds are known.
/// @param a Array element expression on the left-hand side.
/// @param l Enclosing LET statement.
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
    if (auto itType = varTypes_.find(a.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        std::string msg = "variable '" + a.name + "' is not an array";
        de.emit(il::support::Severity::Error,
                "B2001",
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::move(msg));
    }
    auto indexTy = visitExpr(*a.index);
    if (indexTy != Type::Unknown && indexTy != Type::Int)
    {
        std::string msg = "index type mismatch";
        de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
    }
    Type valueTy = Type::Unknown;
    if (l.expr)
    {
        valueTy = visitExpr(*l.expr);
        if (valueTy != Type::Unknown && valueTy != Type::Int)
        {
            std::string msg = "array element type mismatch";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
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
}

/// @brief Handle erroneous LET targets that are not assignable.
/// @details Visits both sides of the expression to surface any nested issues,
///          then emits a diagnostic explaining that the left-hand side must be a
///          variable or array element.
/// @param l LET statement containing the invalid target.
void SemanticAnalyzer::analyzeConstExpr(const LetStmt &l)
{
    if (l.target)
        visitExpr(*l.target);
    if (l.expr)
        visitExpr(*l.expr);
    std::string msg = "left-hand side of LET must be a variable or array element";
    de.emit(il::support::Severity::Error, "B2007", l.loc, 1, std::move(msg));
}

/// @brief Main entry point for analysing LET statements.
/// @details Dispatches to either the scalar or array assignment path depending
///          on the left-hand side expression and reports errors for invalid
///          targets.
/// @param l LET statement AST node.
void SemanticAnalyzer::analyzeLet(LetStmt &l)
{
    if (!l.target)
        return;
    if (auto *v = dynamic_cast<VarExpr *>(l.target.get()))
    {
        analyzeVarAssignment(*v, l);
    }
    else if (auto *a = dynamic_cast<ArrayExpr *>(l.target.get()))
    {
        analyzeArrayAssignment(*a, l);
    }
    else
    {
        analyzeConstExpr(l);
    }
}

/// @brief Analyse the RANDOMIZE statement's optional seed.
/// @details Ensures the seed expression, when present, evaluates to either an
///          integer or floating-point value.  Other types trigger an error.
/// @param r RANDOMIZE statement AST node.
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

/// @brief Analyse DIM declarations for scalar variables and arrays.
/// @details Validates optional size expressions, enforces non-negative array
///          lengths, manages scope-local symbol uniqueness, and updates the
///          analyzer's type and array metadata accordingly.
/// @param d DIM statement AST node.
void SemanticAnalyzer::analyzeDim(DimStmt &d)
{
    long long sz = -1;
    if (d.isArray)
    {
        if (d.size)
        {
            auto ty = visitExpr(*d.size);
            if (ty != Type::Unknown && ty != Type::Int)
            {
                std::string msg = "size type mismatch";
                de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
            }
            if (auto *ci = dynamic_cast<const IntExpr *>(d.size.get()))
            {
                sz = ci->value;
                if (sz < 0)
                {
                    std::string msg = "array size must be non-negative";
                    de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
                }
            }
        }
    }

    if (scopes_.hasScope())
    {
        if (scopes_.isDeclaredInCurrentScope(d.name))
        {
            std::string msg = "duplicate local '" + d.name + "'";
            de.emit(il::support::Severity::Error,
                    "B1006",
                    d.loc,
                    static_cast<uint32_t>(d.name.size()),
                    std::move(msg));
        }
        else
        {
            std::string unique = scopes_.declareLocal(d.name);
            d.name = unique;
            auto insertResult = symbols_.insert(unique);
            if (insertResult.second && activeProcScope_)
                activeProcScope_->noteSymbolInserted(unique);
        }
    }
    else
    {
        auto insertResult = symbols_.insert(d.name);
        if (insertResult.second && activeProcScope_)
            activeProcScope_->noteSymbolInserted(d.name);
    }

    if (d.isArray)
    {
        auto itArray = arrays_.find(d.name);
        if (activeProcScope_)
        {
            std::optional<long long> previous;
            if (itArray != arrays_.end())
                previous = itArray->second;
            activeProcScope_->noteArrayMutation(d.name, previous);
        }
        arrays_[d.name] = sz;

        auto itType = varTypes_.find(d.name);
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            if (itType != varTypes_.end())
                previous = itType->second;
            activeProcScope_->noteVarTypeMutation(d.name, previous);
        }
        varTypes_[d.name] = Type::ArrayInt;
    }
    else
    {
        auto itType = varTypes_.find(d.name);
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            if (itType != varTypes_.end())
                previous = itType->second;
            activeProcScope_->noteVarTypeMutation(d.name, previous);
        }
        varTypes_[d.name] = astToSemanticType(d.type);
    }
}

/// @brief Analyse REDIM statements that resize existing arrays.
/// @details Confirms the size expression is integral and non-negative, ensures
///          the target exists and is an array, and records the new size while
///          tracking mutations for rollback.
/// @param d REDIM statement AST node.
void SemanticAnalyzer::analyzeReDim(ReDimStmt &d)
{
    long long sz = -1;
    if (d.size)
    {
        auto ty = visitExpr(*d.size);
        if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "size type mismatch";
            de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
        }
        if (auto *ci = dynamic_cast<const IntExpr *>(d.size.get()))
        {
            sz = ci->value;
            if (sz < 0)
            {
                std::string msg = "array size must be non-negative";
                de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
            }
        }
    }

    resolveAndTrackSymbol(d.name, SymbolKind::Reference);

    auto itArray = arrays_.find(d.name);
    if (itArray == arrays_.end())
    {
        std::string msg = "unknown array '" + d.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                d.loc,
                static_cast<uint32_t>(d.name.size()),
                std::move(msg));
        return;
    }

    if (auto itType = varTypes_.find(d.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        std::string msg = "REDIM target must be an array";
        de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
        return;
    }

    if (activeProcScope_)
    {
        activeProcScope_->noteArrayMutation(d.name, itArray->second);
    }
    arrays_[d.name] = sz;
}

} // namespace il::frontends::basic
