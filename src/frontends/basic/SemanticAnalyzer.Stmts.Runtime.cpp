//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "frontends/basic/Diag.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
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
        // BUG-037 fix: Detect undefined variables in method calls and suggest qualified call syntax
        if (me->base)
        {
            if (auto *varExpr = as<VarExpr>(*me->base))
            {
                // Check if the base variable exists; if not, this might be a qualified call
                if (symbols_.find(std::string{varExpr->name}) == symbols_.end())
                {
                    // Variable not found - could be a namespace-qualified call attempt
                    std::vector<std::string> segments;
                    segments.push_back(std::string{varExpr->name});
                    segments.push_back(me->method);
                    std::string qualifiedName = CanonicalizeQualified(segments);
                    std::vector<std::string> attempts;
                    diagx::ErrorUnknownProcWithTries(
                        de.emitter(), stmt.loc, qualifiedName, attempts);
                    return;
                }
            }
            visitExpr(*me->base);
        }
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

    // Check if trying to assign to a constant
    if (constants_.find(v.name) != constants_.end())
    {
        std::string msg = "cannot assign to constant '" + v.name + "'";
        de.emit(il::support::Severity::Error, "B2020", l.loc, 1, std::move(msg));
        return;
    }

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

    // If new variable with no suffix and RHS is String, Bool, or Float, pre-set the type
    if (isNewVariable && hasNoSuffix &&
        (exprTy == Type::String || exprTy == Type::Bool || exprTy == Type::Float))
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
    // BUG-056 fix: Check if this is an array field access (e.g., "B.CELLS")
    // If the name contains a dot, it's accessing an array field on an object
    bool isArrayField = a.name.find('.') != std::string::npos;

    if (!isArrayField)
    {
        resolveAndTrackSymbol(a.name, SymbolKind::Reference);
        if (!arrays_.count(a.name))
        {
            de.emit(diag::BasicDiag::UnknownArray,
                    a.loc,
                    static_cast<uint32_t>(a.name.size()),
                    std::initializer_list<diag::Replacement>{diag::Replacement{"name", a.name}});
        }
        if (auto itType = varTypes_.find(a.name); itType != varTypes_.end() &&
                                                  itType->second != Type::ArrayInt &&
                                                  itType->second != Type::ArrayString)
        {
            de.emit(diag::BasicDiag::NotAnArray,
                    a.loc,
                    static_cast<uint32_t>(a.name.size()),
                    std::initializer_list<diag::Replacement>{diag::Replacement{"name", a.name}});
        }
    }
    // For array fields, validation happens during lowering when field types are known

    // Validate each index expression (supports multi-dimensional arrays)
    // For backward compatibility: use 'index' for single-dim, 'indices' for multi-dim
    if (a.index)
    {
        // Single-dimensional array (backward compatible path)
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
    }
    else
    {
        // Multi-dimensional array (new path)
        for (auto &indexPtr : a.indices)
        {
            if (!indexPtr)
                continue;
            auto indexTy = visitExpr(*indexPtr);
            if (indexTy == Type::Float)
            {
                if (auto *floatLiteral = as<FloatExpr>(*indexPtr))
                {
                    insertImplicitCast(*indexPtr, Type::Int);
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
        }
    }

    Type valueTy = Type::Unknown;
    if (l.expr)
    {
        valueTy = visitExpr(*l.expr);

        // Determine expected element type based on array type
        Type expectedElementType = Type::Int; // default for integer arrays
        if (auto itType = varTypes_.find(a.name); itType != varTypes_.end())
        {
            if (itType->second == Type::ArrayString)
                expectedElementType = Type::String;
        }

        if (expectedElementType == Type::Int)
        {
            // Integer array: allow Int or Float (with warning for Float)
            if (valueTy == Type::Float)
            {
                markImplicitConversion(*l.expr, Type::Int);
                std::string msg = "narrowing conversion from FLOAT to INT in array assignment";
                de.emit(il::support::Severity::Warning, "B2002", l.loc, 1, std::move(msg));
            }
            else if (valueTy != Type::Unknown && valueTy != Type::Int)
            {
                std::string msg = "array element type mismatch: expected INT, got ";
                msg += semanticTypeName(valueTy);
                de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
            }
        }
        else if (expectedElementType == Type::String)
        {
            // String array: require String type
            if (valueTy != Type::Unknown && valueTy != Type::String)
            {
                std::string msg = "array element type mismatch: expected STRING, got ";
                msg += semanticTypeName(valueTy);
                de.emit(il::support::Severity::Error, "B2001", l.loc, 1, std::move(msg));
            }
        }
    }
    auto it = arrays_.find(a.name);
    if (it != arrays_.end() && !it->second.extents.empty() && it->second.extents.size() == 1 &&
        a.index)
    {
        // Bounds check for single-dimensional arrays
        long long arraySize = it->second.extents[0];
        if (arraySize >= 0)
        {
            if (auto *ci = as<const IntExpr>(*a.index))
            {
                if (ci->value < 0 || ci->value >= arraySize)
                {
                    std::string msg = "index out of bounds";
                    de.emit(il::support::Severity::Warning, "B3001", a.loc, 1, std::move(msg));
                }
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
    else if (auto *mc = as<MethodCallExpr>(*l.target))
    {
        // BUG-056: Treat method-like syntax on LHS (obj.field(...)) as array-field assignment.
        // Perform basic index type validation and RHS checks similar to arrays.
        if (mc->base)
            visitExpr(*mc->base);
        // Validate indices: all args must be INT (allow float literals with narrowing warning)
        for (auto &arg : mc->args)
        {
            if (!arg)
                continue;
            Type ty = visitExpr(*arg);
            if (ty == Type::Float)
            {
                if (auto *fl = as<FloatExpr>(*arg))
                {
                    insertImplicitCast(*arg, Type::Int);
                    std::string msg = "narrowing conversion from FLOAT to INT in array index";
                    de.emit(il::support::Severity::Warning, "B2002", mc->loc, 1, std::move(msg));
                }
                else
                {
                    std::string msg = "index type mismatch";
                    de.emit(il::support::Severity::Error, "B2001", mc->loc, 1, std::move(msg));
                }
            }
            else if (ty != Type::Unknown && ty != Type::Int)
            {
                std::string msg = "index type mismatch";
                de.emit(il::support::Severity::Error, "B2001", mc->loc, 1, std::move(msg));
            }
        }
        // Analyze RHS to surface type errors; element type will be validated during lowering
        if (l.expr)
            visitExpr(*l.expr);
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
    ArrayMetadata metadata;

    if (d.isArray)
    {
        // Collect dimension expressions: check 'size' first (backward compat), then 'dimensions'
        std::vector<const ExprPtr *> dimExprs;
        if (d.size)
        {
            dimExprs.push_back(&d.size);
        }
        else if (!d.dimensions.empty())
        {
            for (const auto &dimExpr : d.dimensions)
            {
                if (dimExpr)
                    dimExprs.push_back(&dimExpr);
            }
        }

        // Validate and extract extent values
        std::vector<long long> extents;
        bool allConstant = true;

        for (const ExprPtr *dimExprPtr : dimExprs)
        {
            const ExprPtr &dimExpr = *dimExprPtr;
            FloatExpr *floatLiteral = nullptr;
            auto ty = visitExpr(*dimExpr);

            // Type checking
            if (ty == Type::Float)
            {
                floatLiteral = as<FloatExpr>(*dimExpr);
                if (floatLiteral != nullptr)
                {
                    insertImplicitCast(*dimExpr, Type::Int);
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

            // Extract constant extent value
            if (floatLiteral)
            {
                if (floatLiteral->value < 0.0)
                {
                    std::string msg = "array extent must be non-negative";
                    de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
                }
                else
                {
                    extents.push_back(static_cast<long long>(floatLiteral->value));
                }
            }
            else if (auto *ci = as<const IntExpr>(*dimExpr))
            {
                long long extent = ci->value;
                if (extent < 0)
                {
                    std::string msg = "array extent must be non-negative";
                    de.emit(il::support::Severity::Error, "B2003", d.loc, 1, std::move(msg));
                }
                extents.push_back(extent);
            }
            else
            {
                // Runtime-computed extent
                allConstant = false;
            }
        }

        // Compute total size if all extents are constant
        if (allConstant && !extents.empty())
        {
            long long totalSize = 1;
            bool overflow = false;

            for (long long extent : extents)
            {
                // Check for overflow: totalSize * extent > LLONG_MAX
                if (extent > 0 && totalSize > LLONG_MAX / extent)
                {
                    overflow = true;
                    break;
                }
                totalSize *= extent;
            }

            if (overflow)
            {
                std::string msg = "array size computation overflows";
                de.emit(il::support::Severity::Error, "B2004", d.loc, 1, std::move(msg));
                metadata = ArrayMetadata(); // dynamic/unknown
            }
            else
            {
                metadata = ArrayMetadata(std::move(extents), totalSize);
            }
        }
        else if (!extents.empty())
        {
            // Partial constant extents - store what we know but mark total as dynamic
            metadata.extents = std::move(extents);
            metadata.totalSize = -1;
        }
        // else: all dynamic, leave metadata as default (empty extents, totalSize=-1)
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
            std::optional<ArrayMetadata> previous;
            if (itArray != arrays_.end())
                previous = itArray->second;
            activeProcScope_->noteArrayMutation(d.name, previous);
        }
        arrays_[d.name] = metadata;

        auto itType = varTypes_.find(d.name);
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            if (itType != varTypes_.end())
                previous = itType->second;
            activeProcScope_->noteVarTypeMutation(d.name, previous);
        }
        // Determine array element type preferring explicit AS clause over name suffix.
        // Object arrays are tracked separately; here we only distinguish STRING vs numeric.
        if (!d.explicitClassQname.empty())
        {
            // Treat object arrays as numeric for analyzer typing; element checks occur elsewhere.
            varTypes_[d.name] = Type::ArrayInt;
        }
        else if (d.type == ::il::frontends::basic::Type::Str ||
                 (!d.name.empty() && d.name.back() == '$'))
        {
            varTypes_[d.name] = Type::ArrayString;
        }
        else
        {
            varTypes_[d.name] = Type::ArrayInt;
        }
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

/// @brief Validate CONST statements and track constant names.
///
/// @param c CONST statement declaring a constant.
void SemanticAnalyzer::analyzeConst(ConstStmt &c)
{
    // Evaluate the initializer expression to determine its type
    Type initializerTy = Type::Unknown;
    if (c.initializer)
    {
        initializerTy = visitExpr(*c.initializer);
    }

    // Track this name as a constant
    constants_.insert(c.name);

    // Also track it as a symbol
    auto insertResult = symbols_.insert(c.name);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteSymbolInserted(c.name);

    // Determine final type: infer from initializer if no explicit suffix/AS clause
    // Check if constant name has no type suffix (ends with alphanumeric)
    bool hasNoSuffix = !c.name.empty() && std::isalnum(static_cast<unsigned char>(c.name.back()));

    Type finalType = astToSemanticType(c.type);

    // If no suffix and initializer is Float, infer Float type (BUG-019 fix)
    if (hasNoSuffix && c.type == ::il::frontends::basic::Type::I64 && initializerTy == Type::Float)
    {
        finalType = Type::Float;
    }

    // Track the type
    auto itType = varTypes_.find(c.name);
    if (activeProcScope_)
    {
        std::optional<Type> previous;
        if (itType != varTypes_.end())
            previous = itType->second;
        activeProcScope_->noteVarTypeMutation(c.name, previous);
    }
    varTypes_[c.name] = finalType;
}

/// @brief Analyze a STATIC statement declaring procedure-local persistent variables.
/// @details STATIC variables are procedure-scoped like DIM, but their storage persists
///          between calls. This analyzer registers the variable name in the current scope.
/// @param s STATIC statement to validate and register.
void SemanticAnalyzer::analyzeStatic(StaticStmt &s)
{
    if (scopes_.hasScope())
    {
        if (scopes_.isDeclaredInCurrentScope(s.name))
        {
            std::string msg = "duplicate local '" + s.name + "'";
            de.emit(il::support::Severity::Error,
                    "B1006",
                    s.loc,
                    static_cast<uint32_t>(s.name.size()),
                    std::move(msg));
        }
        else
        {
            std::string unique = scopes_.declareLocal(s.name);
            s.name = unique;
            auto insertResult = symbols_.insert(unique);
            if (insertResult.second && activeProcScope_)
                activeProcScope_->noteSymbolInserted(unique);
        }
    }
    else
    {
        auto insertResult = symbols_.insert(s.name);
        if (insertResult.second && activeProcScope_)
            activeProcScope_->noteSymbolInserted(s.name);
    }
}

/// @brief Analyze a SHARED statement listing names that refer to module-level state.
/// @details For compatibility: procedures can already access module-level globals without SHARED.
///          This handler resolves each name to ensure diagnostics include the correct symbol,
///          and records a reference so later passes materialize storage as needed.
/// @param s SHARED statement being analyzed.
void SemanticAnalyzer::analyzeShared(SharedStmt &s)
{
    for (auto &name : s.names)
    {
        resolveAndTrackSymbol(name, SymbolKind::Reference);
        // Record as a known symbol; do not allocate local storage here.
        auto insertResult = symbols_.insert(name);
        if (insertResult.second && activeProcScope_)
            activeProcScope_->noteSymbolInserted(name);
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
    // REDIM changes size - update metadata with new single-dimension size
    arrays_[d.name] = ArrayMetadata(sz);
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
