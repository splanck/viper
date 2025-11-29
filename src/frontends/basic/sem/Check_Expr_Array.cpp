//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/Check_Expr_Array.cpp
// Purpose: Validate BASIC array access expressions and infer their element type
//          during semantic analysis.
// Key invariants:
//   * Array references are resolved against the symbol table so undefined
//     arrays are detected early.
//   * Index expressions must be integers; float indices trigger warnings.
//   * Bounds checking is performed for constant indices when extents are known.
// References: docs/codemap/basic.md, docs/basic-language.md#arrays
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic analysis helper for array access expressions.
/// @details Resolves array symbols, validates indices, and performs static
///          bounds checking where possible.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/Diag.hpp"
#include "frontends/basic/sem/Check_Common.hpp"

namespace il::frontends::basic::sem
{

/// @brief Validate an array index expression and emit diagnostics as needed.
///
/// @param context Expression checking context.
/// @param indexExpr Index expression to validate.
/// @param arrayLoc Source location of the array access.
/// @return Type of the index expression.
static SemanticAnalyzer::Type validateArrayIndex(ExprCheckContext &context,
                                                 Expr &indexExpr,
                                                 il::support::SourceLoc arrayLoc)
{
    using Type = SemanticAnalyzer::Type;

    Type ty = context.evaluate(indexExpr);
    if (ty == Type::Float)
    {
        if (as<FloatExpr>(indexExpr) != nullptr)
        {
            context.insertImplicitCast(indexExpr, Type::Int);
            std::string msg = "narrowing conversion from FLOAT to INT in array index";
            context.diagnostics().emit(
                il::support::Severity::Warning, "B2002", arrayLoc, 1, std::move(msg));
        }
        else
        {
            std::string msg = "index type mismatch";
            context.diagnostics().emit(
                il::support::Severity::Error, "B2001", arrayLoc, 1, std::move(msg));
        }
    }
    else if (ty != Type::Unknown && ty != Type::Int)
    {
        std::string msg = "index type mismatch";
        context.diagnostics().emit(
            il::support::Severity::Error, "B2001", arrayLoc, 1, std::move(msg));
    }
    return ty;
}

/// @brief Type-check a BASIC array access expression and compute its element type.
///
/// @details Validates that the symbol is a known array, checks index types,
///          and performs static bounds checking for constant indices.
///
/// @param analyzer Semantic analyzer coordinating the current compilation.
/// @param expr Array expression to validate.
/// @return Semantic type of the array element (or Unknown if errors occurred).
SemanticAnalyzer::Type analyzeArrayExpr(SemanticAnalyzer &analyzer, ArrayExpr &expr)
{
    using Type = SemanticAnalyzer::Type;

    ExprCheckContext context(analyzer);
    context.resolveAndTrackSymbolRef(expr.name);

    if (!context.hasArray(expr.name))
    {
        context.diagnostics().emit(
            diag::BasicDiag::UnknownArray,
            expr.loc,
            static_cast<uint32_t>(expr.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});

        // Visit all indices for type checking even on error path
        if (expr.index)
            context.evaluate(*expr.index);
        for (auto &indexPtr : expr.indices)
        {
            if (indexPtr)
                context.evaluate(*indexPtr);
        }
        return Type::Unknown;
    }

    auto varTy = context.varType(expr.name);
    if (varTy && *varTy != Type::ArrayInt && *varTy != Type::ArrayString)
    {
        context.diagnostics().emit(
            diag::BasicDiag::NotAnArray,
            expr.loc,
            static_cast<uint32_t>(expr.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});

        // Visit all indices for type checking even on error path
        if (expr.index)
            context.evaluate(*expr.index);
        for (auto &indexPtr : expr.indices)
        {
            if (indexPtr)
                context.evaluate(*indexPtr);
        }
        return Type::Unknown;
    }

    // Validate indices
    if (expr.index)
    {
        // Single-dimensional array (backward compatible path)
        validateArrayIndex(context, *expr.index, expr.loc);

        // Bounds check for single-dimensional arrays
        const auto *meta = context.arrayMetadata(expr.name);
        if (meta && !meta->extents.empty() && meta->extents.size() == 1)
        {
            long long arraySize = meta->extents[0];
            if (arraySize >= 0)
            {
                if (auto *ci = as<const IntExpr>(*expr.index))
                {
                    if (ci->value < 0 || ci->value >= arraySize)
                    {
                        std::string msg = "index out of bounds";
                        context.diagnostics().emit(
                            il::support::Severity::Warning, "B3001", expr.loc, 1, std::move(msg));
                    }
                }
            }
        }
    }
    else
    {
        // Multi-dimensional array (new path)
        for (auto &indexPtr : expr.indices)
        {
            if (indexPtr)
                validateArrayIndex(context, *indexPtr, expr.loc);
        }
        // TODO: Bounds check for multi-dimensional arrays
    }

    // Return element type based on array type
    if (varTy && *varTy == Type::ArrayString)
        return Type::String;

    return Type::Int;
}

/// @brief Analyse an LBOUND expression returning the lower index bound.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param expr LBOUND expression node.
/// @return Integer type on success or Unknown when diagnostics were emitted.
SemanticAnalyzer::Type analyzeLBoundExpr(SemanticAnalyzer &analyzer, LBoundExpr &expr)
{
    using Type = SemanticAnalyzer::Type;

    ExprCheckContext context(analyzer);
    context.resolveAndTrackSymbolRef(expr.name);

    if (!context.hasArray(expr.name))
    {
        context.diagnostics().emit(
            diag::BasicDiag::UnknownArray,
            expr.loc,
            static_cast<uint32_t>(expr.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }

    auto varTy = context.varType(expr.name);
    if (varTy && *varTy != Type::ArrayInt)
    {
        context.diagnostics().emit(
            diag::BasicDiag::NotAnArray,
            expr.loc,
            static_cast<uint32_t>(expr.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }

    return Type::Int;
}

/// @brief Analyse a UBOUND expression returning the upper index bound.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param expr UBOUND expression node.
/// @return Integer type on success or Unknown when diagnostics were emitted.
SemanticAnalyzer::Type analyzeUBoundExpr(SemanticAnalyzer &analyzer, UBoundExpr &expr)
{
    using Type = SemanticAnalyzer::Type;

    ExprCheckContext context(analyzer);
    context.resolveAndTrackSymbolRef(expr.name);

    if (!context.hasArray(expr.name))
    {
        context.diagnostics().emit(
            diag::BasicDiag::UnknownArray,
            expr.loc,
            static_cast<uint32_t>(expr.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }

    auto varTy = context.varType(expr.name);
    if (varTy && *varTy != Type::ArrayInt)
    {
        context.diagnostics().emit(
            diag::BasicDiag::NotAnArray,
            expr.loc,
            static_cast<uint32_t>(expr.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name}});
        return Type::Unknown;
    }

    return Type::Int;
}

} // namespace il::frontends::basic::sem
