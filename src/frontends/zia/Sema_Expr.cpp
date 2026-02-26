//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Expr.cpp
/// @brief Expression analysis dispatcher and literal analysis for the Zia
///        semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"

namespace il::frontends::zia
{

//=============================================================================
// Expression Analysis Dispatcher
//=============================================================================

/// @brief Main entry point for expression analysis.
/// @param expr The expression AST node to analyze.
/// @return The resolved semantic type for the expression.
/// @details Dispatches to specific analysis methods based on expression kind.
///          Caches the result in exprTypes_ for later retrieval.
TypeRef Sema::analyzeExpr(Expr *expr)
{
    if (!expr)
        return types::unknown();

    TypeRef result;

    switch (expr->kind)
    {
        case ExprKind::IntLiteral:
            result = analyzeIntLiteral(static_cast<IntLiteralExpr *>(expr));
            break;
        case ExprKind::NumberLiteral:
            result = analyzeNumberLiteral(static_cast<NumberLiteralExpr *>(expr));
            break;
        case ExprKind::StringLiteral:
            result = analyzeStringLiteral(static_cast<StringLiteralExpr *>(expr));
            break;
        case ExprKind::BoolLiteral:
            result = analyzeBoolLiteral(static_cast<BoolLiteralExpr *>(expr));
            break;
        case ExprKind::NullLiteral:
            result = analyzeNullLiteral(static_cast<NullLiteralExpr *>(expr));
            break;
        case ExprKind::UnitLiteral:
            result = analyzeUnitLiteral(static_cast<UnitLiteralExpr *>(expr));
            break;
        case ExprKind::Ident:
            result = analyzeIdent(static_cast<IdentExpr *>(expr));
            break;
        case ExprKind::SelfExpr:
            result = analyzeSelf(static_cast<SelfExpr *>(expr));
            break;
        case ExprKind::Binary:
            result = analyzeBinary(static_cast<BinaryExpr *>(expr));
            break;
        case ExprKind::Unary:
            result = analyzeUnary(static_cast<UnaryExpr *>(expr));
            break;
        case ExprKind::Ternary:
            result = analyzeTernary(static_cast<TernaryExpr *>(expr));
            break;
        case ExprKind::If:
            result = analyzeIfExpr(static_cast<IfExpr *>(expr));
            break;
        case ExprKind::StructLiteral:
            result = analyzeStructLiteral(static_cast<StructLiteralExpr *>(expr));
            break;
        case ExprKind::Call:
            result = analyzeCall(static_cast<CallExpr *>(expr));
            break;
        case ExprKind::Index:
            result = analyzeIndex(static_cast<IndexExpr *>(expr));
            break;
        case ExprKind::Field:
            result = analyzeField(static_cast<FieldExpr *>(expr));
            break;
        case ExprKind::OptionalChain:
            result = analyzeOptionalChain(static_cast<OptionalChainExpr *>(expr));
            break;
        case ExprKind::Coalesce:
            result = analyzeCoalesce(static_cast<CoalesceExpr *>(expr));
            break;
        case ExprKind::Is:
            result = analyzeIs(static_cast<IsExpr *>(expr));
            break;
        case ExprKind::As:
            result = analyzeAs(static_cast<AsExpr *>(expr));
            break;
        case ExprKind::Range:
            result = analyzeRange(static_cast<RangeExpr *>(expr));
            break;
        case ExprKind::New:
            result = analyzeNew(static_cast<NewExpr *>(expr));
            break;
        case ExprKind::Lambda:
            result = analyzeLambda(static_cast<LambdaExpr *>(expr));
            break;
        case ExprKind::Match:
            result = analyzeMatchExpr(static_cast<MatchExpr *>(expr));
            break;
        case ExprKind::ListLiteral:
            result = analyzeListLiteral(static_cast<ListLiteralExpr *>(expr));
            break;
        case ExprKind::MapLiteral:
            result = analyzeMapLiteral(static_cast<MapLiteralExpr *>(expr));
            break;
        case ExprKind::SetLiteral:
            result = analyzeSetLiteral(static_cast<SetLiteralExpr *>(expr));
            break;
        case ExprKind::Tuple:
            result = analyzeTuple(static_cast<TupleExpr *>(expr));
            break;
        case ExprKind::TupleIndex:
            result = analyzeTupleIndex(static_cast<TupleIndexExpr *>(expr));
            break;
        case ExprKind::Block:
            result = analyzeBlockExpr(static_cast<BlockExpr *>(expr));
            break;
        default:
            result = types::unknown();
            break;
    }

    exprTypes_[expr] = result;
    return result;
}

//=============================================================================
// Literal Analysis
//=============================================================================

/// @brief Analyze an integer literal expression.
/// @return The Integer type singleton.
TypeRef Sema::analyzeIntLiteral(IntLiteralExpr * /*expr*/)
{
    return types::integer();
}

/// @brief Analyze a floating-point number literal expression.
/// @return The Number type singleton.
TypeRef Sema::analyzeNumberLiteral(NumberLiteralExpr * /*expr*/)
{
    return types::number();
}

/// @brief Analyze a string literal expression.
/// @return The String type singleton.
TypeRef Sema::analyzeStringLiteral(StringLiteralExpr * /*expr*/)
{
    return types::string();
}

/// @brief Analyze a boolean literal expression (true/false).
/// @return The Boolean type singleton.
TypeRef Sema::analyzeBoolLiteral(BoolLiteralExpr * /*expr*/)
{
    return types::boolean();
}

/// @brief Analyze a null literal expression.
/// @return Optional[Unknown] type; actual type determined by context.
TypeRef Sema::analyzeNullLiteral(NullLiteralExpr * /*expr*/)
{
    // null is Optional[Unknown] - needs context to determine actual type
    return types::optional(types::unknown());
}

/// @brief Analyze a unit literal expression ().
/// @return The Unit type singleton.
TypeRef Sema::analyzeUnitLiteral(UnitLiteralExpr * /*expr*/)
{
    return types::unit();
}

/// @brief Analyze an identifier expression.
/// @param expr The identifier expression node.
/// @return The type bound to the identifier, or Unknown if undefined.
/// @details Looks up the identifier in the symbol table and imported symbols.
///          For imported runtime classes, returns a module-like type.
TypeRef Sema::analyzeIdent(IdentExpr *expr)
{
    Symbol *sym = lookupSymbol(expr->name);
    if (!sym)
    {
        // Check if this is an imported symbol from a bound namespace
        auto importIt = importedSymbols_.find(expr->name);
        if (importIt != importedSymbols_.end())
        {
            const std::string &fullName = importIt->second;
            if (fullName.rfind("Viper.", 0) == 0)
            {
                // Check if it's a zero-arg getter function (e.g., Viper.Math.get_Pi)
                // If so, treat it as an auto-evaluated property
                Symbol *fnSym = lookupSymbol(fullName);
                if (fnSym && fnSym->kind == Symbol::Kind::Function && fnSym->isExtern)
                {
                    autoEvalGetters_[expr] = fullName;
                    return fnSym->type;
                }
                // For imported runtime classes, return a module-like type so that
                // field access (e.g., Canvas.New) can be resolved
                return types::module(fullName);
            }
        }

        errorUndefined(expr->loc, expr->name);
        return types::unknown();
    }

    // For variables and parameters, respect flow-sensitive type narrowing
    // (e.g., after `if x != null`, x is narrowed from T? to T)
    if (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Parameter)
    {
        // Warn if variable used before initialization
        if (sym->kind == Symbol::Kind::Variable && !isInitialized(expr->name))
        {
            warning(expr->loc,
                    "Variable '" + expr->name + "' may be used before initialization");
        }

        TypeRef narrowed = lookupVarType(expr->name);
        if (narrowed)
            return narrowed;
    }

    return sym->type;
}

/// @brief Analyze a 'self' expression.
/// @param expr The self expression node.
/// @return The type of 'self' in the current method context.
/// @details Emits error if used outside a method body.
TypeRef Sema::analyzeSelf(SelfExpr *expr)
{
    if (!currentSelfType_)
    {
        error(expr->loc, "'self' can only be used inside a method");
        return types::unknown();
    }
    return currentSelfType_;
}

} // namespace il::frontends::zia
