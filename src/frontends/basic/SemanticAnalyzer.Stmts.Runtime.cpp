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

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

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
    const ProcSignature *sig = resolveCallee(*stmt.call, ProcSignature::Kind::Sub);
    checkCallArgs(*stmt.call, sig);
}

/// @brief Check type rules and loop-variable restrictions for scalar assignments.
///
/// @param v Variable expression receiving the assignment.
/// @param l LET statement providing the assigned expression and location.
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
    if (indexTy == Type::Float)
    {
        if (auto *floatLiteral = dynamic_cast<FloatExpr *>(a.index.get()))
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

/// @brief Emit diagnostics when the left-hand side of a LET is not assignable.
///
/// @param l LET statement with a non-variable target.
void SemanticAnalyzer::analyzeConstExpr(const LetStmt &l)
{
    if (l.target)
        visitExpr(*l.target);
    if (l.expr)
        visitExpr(*l.expr);
    std::string msg = "left-hand side of LET must be a variable or array element";
    de.emit(il::support::Severity::Error, "B2007", l.loc, 1, std::move(msg));
}

/// @brief Dispatch LET statement analysis based on target form.
///
/// @param l LET statement being validated.
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
                floatLiteral = dynamic_cast<FloatExpr *>(d.size.get());
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
            else if (auto *ci = dynamic_cast<const IntExpr *>(d.size.get()))
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
            floatLiteral = dynamic_cast<FloatExpr *>(d.size.get());
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
        else if (auto *ci = dynamic_cast<const IntExpr *>(d.size.get()))
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
