//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer.Stmts.Runtime.cpp
// Purpose: Implements runtime/data-manipulation statement checks for the BASIC
//          semantic analyzer, covering LET/DIM/REDIM, RANDOMIZE, and CALL.
// Key invariants: Shared helpers guard loop-variable mutations and array/type
//                 tracking remains in sync with procedure scopes.
// Ownership/Lifetime: Borrowed SemanticAnalyzer state only.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements semantic validation for BASIC statements that manipulate runtime state.
/// @details These helpers cover assignments, array declarations, random seeds, and
///          procedure calls while maintaining the analyzer's bookkeeping.

#include "frontends/basic/SemanticAnalyzer.Stmts.Runtime.hpp"
#include "frontends/basic/ASTUtils.hpp"

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Bind runtime statement helpers to the active semantic analyzer state.
///
/// @param analyzer Analyzer that supplies shared context such as loop tracking.
RuntimeStmtContext::RuntimeStmtContext(SemanticAnalyzer &analyzer) noexcept : StmtShared(analyzer)
{
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::astToSemanticType;
using semantic_analyzer_detail::RuntimeStmtContext;
using semantic_analyzer_detail::semanticTypeName;

/// @brief Validate a CALL statement against registered procedure signatures.
///
/// @param stmt Statement node describing the call.
void SemanticAnalyzer::analyzeCallStmt(CallStmt &stmt)
{
    if (!stmt.call)
        return;

    if (auto *ce = as<CallExpr>(*stmt.call))
    {
        // Statement calls must target SUBs (not FUNCTIONs)
        const ProcSignature *sig = resolveCallee(*ce, ProcSignature::Kind::Sub);
        checkCallArgs(*ce, sig);
        return;
    }

    if (auto *me = as<MethodCallExpr>(*stmt.call))
    {
        // Best-effort analysis: visit receiver and args to trigger diagnostics.
        if (me->base)
            visitExpr(*me->base);
        for (auto &arg : me->args)
            if (arg)
                visitExpr(*arg);
        return;
    }

    // Unknown invocation node: nothing to analyze (defensive).
}

/// @brief Check type rules and loop-variable restrictions for scalar assignments.
///
/// @param v Variable expression receiving the assignment.
/// @param l LET statement providing the assigned expression and location.
void SemanticAnalyzer::analyzeVarAssignment(VarExpr &v, const LetStmt &l)
{
    RuntimeStmtContext ctx(*this);

    // BUG-003: Check if this is an assignment to the function name (VB-style implicit return)
    if (activeFunction_ && string_utils::iequals(v.name, activeFunction_->name))
    {
        activeFunctionNameAssigned_ = true;
    }

    // BUG-001 FIX: Evaluate RHS expression BEFORE resolving variable
    // This allows us to infer the variable's type from the RHS
    Type exprTy = Type::Unknown;
    if (l.expr)
        exprTy = visitExpr(*l.expr);

    // Check if variable has no type suffix (ends with alphanumeric, not $#!%&)
    bool hasNoSuffix = !v.name.empty() && std::isalnum(static_cast<unsigned char>(v.name.back()));

    // Check if variable already exists
    bool isNewVariable = (varTypes_.find(v.name) == varTypes_.end());

    // If new variable with no suffix and RHS is String or Bool, pre-set the type
    if (isNewVariable && hasNoSuffix && (exprTy == Type::String || exprTy == Type::Bool))
    {
        varTypes_[v.name] = exprTy;
    }

    resolveAndTrackSymbol(v.name, SymbolKind::Definition);
    if (ctx.isLoopVariable(v.name))
        ctx.reportLoopVariableMutation(v.name, l.loc, static_cast<uint32_t>(v.name.size()));

    Type varTy = Type::Int;
    if (auto itType = varTypes_.find(v.name); itType != varTypes_.end())
        varTy = itType->second;

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
            if (const auto *bin = as<const BinaryExpr>(*l.expr))
            {
                const bool hasExplicitIntSuffix =
                    !v.name.empty() && (v.name.back() == '%' || v.name.back() == '&');
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

/// @brief Validate assignments targeting array elements.
///
/// @param a Array expression identifying the element being written.
/// @param l LET statement describing the overall assignment.
void SemanticAnalyzer::analyzeArrayAssignment(ArrayExpr &a, const LetStmt &l)
{
    resolveAndTrackSymbol(a.name, SymbolKind::Reference);
    if (!arrays_.count(a.name))
    {
        de.emit(diag::BasicDiag::UnknownArray,
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", a.name}});
    }
    if (auto itType = varTypes_.find(a.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        de.emit(diag::BasicDiag::NotAnArray,
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", a.name}});
    }
    auto indexTy = visitExpr(*a.index);
    if (indexTy == Type::Float)
    {
        if (auto *floatLiteral = as<FloatExpr>(*a.index))
        {
            insertImplicitCast(*a.index, Type::Int);
            std::string msg = "narrowing conversion from FLOAT to INT in array index";
            de.emit(il::support::Severity::Warning, "B2002", a.loc, 1, std::move(msg));
        }
        else
        {
            std::string msg = "index type mismatch";
            de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
        }
    }
    else if (indexTy != Type::Unknown && indexTy != Type::Int)
    {
        std::string msg = "index type mismatch";
        de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
    }
    Type valueTy = Type::Unknown;
    if (l.expr)
    {
        valueTy = visitExpr(*l.expr);
        if (valueTy == Type::Float)
        {
            markImplicitConversion(*l.expr, Type::Int);
            std::string msg = "narrowing conversion from FLOAT to INT in array assignment";
            de.emit(il::support::Severity::Warning, "B2002", l.loc, 1, std::move(msg));
        }
        else if (valueTy != Type::Unknown && valueTy != Type::Int)
        {
            std::string msg = "array element type mismatch";
            de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
        }
    }
    auto it = arrays_.find(a.name);
    if (it != arrays_.end() && it->second >= 0)
    {
        if (auto *ci = as<const IntExpr>(*a.index))
        {
            if (ci->value < 0 || ci->value >= it->second)
            {
                std::string msg = "index out of bounds";
                de.emit(il::support::Severity::Warning, "B3001", a.loc, 1, std::move(msg));
            }
        }
    }
}

void SemanticAnalyzer::analyzeMemberAssignment(MemberAccessExpr &m, const LetStmt &l)
{
    if (m.base)
        visitExpr(*m.base);
    if (l.expr)
        visitExpr(*l.expr);
}

/// @brief Emit diagnostics when the left-hand side of a LET is not assignable.
///
/// @param l LET statement with a non-variable target.
void SemanticAnalyzer::analyzeConstExpr(const LetStmt &l)
{
    if (l.target)
        visitExpr(*l.target);
    if (l.expr)
        visitExpr(*l.expr);
    std::string msg = "left-hand side of LET must be a variable, array element, or object field";
    de.emit(il::support::Severity::Error, "B2007", l.loc, 1, std::move(msg));
}

/// @brief Dispatch LET statement analysis based on target form.
///
/// @param l LET statement being validated.
void SemanticAnalyzer::analyzeLet(LetStmt &l)
{
    if (!l.target)
        return;
    if (auto *v = as<VarExpr>(*l.target))
    {
        analyzeVarAssignment(*v, l);
    }
    else if (auto *a = as<ArrayExpr>(*l.target))
    {
        analyzeArrayAssignment(*a, l);
    }
    else if (auto *m = as<MemberAccessExpr>(*l.target))
    {
        analyzeMemberAssignment(*m, l);
    }
    else
    {
        analyzeConstExpr(l);
    }
}

/// @brief Validate RANDOMIZE statements and seed expressions.
///
/// @param r RANDOMIZE statement node.
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

/// @brief Validate DIM statements and update analyzer state.
///
/// @param d DIM statement describing variable or array declarations.
void SemanticAnalyzer::analyzeDim(DimStmt &d)
{
    long long sz = -1;
    if (d.isArray)
    {
        if (d.size)
        {
            FloatExpr *floatLiteral = nullptr;
            auto ty = visitExpr(*d.size);
            if (ty == Type::Float)
            {
                floatLiteral = as<FloatExpr>(*d.size);
                if (floatLiteral != nullptr)
                {
                    insertImplicitCast(*d.size, Type::Int);
                    std::string msg = "narrowing conversion from FLOAT to INT in array size";
                    de.emit(il::support::Severity::Warning, "B2002", d.loc, 1, std::move(msg));
                }
                else
                {
                    std::string msg = "size type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
                }
            }
            else if (ty != Type::Unknown && ty != Type::Int)
            {
                std::string msg = "size type mismatch";
                de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
            }
            if (floatLiteral)
            {
                if (floatLiteral->value < 0.0)
                {
                    std::string msg = "array size must be non-negative";
                    de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
                }
            }
            else if (auto *ci = as<const IntExpr>(*d.size))
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

/// @brief Validate REDIM statements for previously declared arrays.
///
/// @param d REDIM statement describing the new array bounds.
void SemanticAnalyzer::analyzeReDim(ReDimStmt &d)
{
    long long sz = -1;
    if (d.size)
    {
        FloatExpr *floatLiteral = nullptr;
        auto ty = visitExpr(*d.size);
        if (ty == Type::Float)
        {
            floatLiteral = as<FloatExpr>(*d.size);
            if (floatLiteral != nullptr)
            {
                insertImplicitCast(*d.size, Type::Int);
                std::string msg = "narrowing conversion from FLOAT to INT in array size";
                de.emit(il::support::Severity::Warning, "B2002", d.loc, 1, std::move(msg));
            }
            else
            {
                std::string msg = "size type mismatch";
                de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
            }
        }
        else if (ty != Type::Unknown && ty != Type::Int)
        {
            std::string msg = "size type mismatch";
            de.emit(il::support::Severity::Error, "B2001", d.loc, 1, std::move(msg));
        }
        if (floatLiteral)
        {
            if (floatLiteral->value < 0.0)
            {
                std::string msg = "array size must be non-negative";
                de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
            }
        }
        else if (auto *ci = as<const IntExpr>(*d.size))
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
        de.emit(diag::BasicDiag::UnknownArray,
                d.loc,
                static_cast<uint32_t>(d.name.size()),
                std::initializer_list<diag::Replacement>{diag::Replacement{"name", d.name}});
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

/// @brief Validate SWAP statements for compatible types.
///
/// @param s SWAP statement describing the two lvalues to exchange.
void SemanticAnalyzer::analyzeSwap(SwapStmt &s)
{
    if (s.lhs)
    {
        visitExpr(*s.lhs);
    }
    if (s.rhs)
    {
        visitExpr(*s.rhs);
    }
}

} // namespace il::frontends::basic
