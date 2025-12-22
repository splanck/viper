//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/Check_Expr_Var.cpp
// Purpose: Validate BASIC variable references and infer their type during
//          semantic analysis.
// Key invariants:
//   * Variable references are resolved against the symbol table so undefined
//     variables are detected early.
//   * Levenshtein suggestions help users fix typos in variable names.
//   * BASIC suffix rules ($ for string, #/! for float) provide implicit types.
// References: docs/codemap/basic.md, docs/basic-language.md#variables
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic analysis helper for variable reference expressions.
/// @details Resolves variable symbols, suggests corrections for unknown names,
///          and applies BASIC type suffix conventions.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Diag.hpp"
#include "frontends/basic/sem/Check_Common.hpp"

#include <limits>

namespace il::frontends::basic::sem
{

/// @brief Type-check a BASIC variable reference and compute its type.
///
/// @details The helper performs variable resolution and emits diagnostics when
///          the variable is unknown. It suggests close matches using Levenshtein
///          distance and applies BASIC type suffix conventions for implicit types.
///
/// @param analyzer Semantic analyzer coordinating the current compilation.
/// @param expr Variable expression to validate.
/// @return Semantic type of the variable (or Unknown if undefined).
SemanticAnalyzer::Type analyzeVarExpr(SemanticAnalyzer &analyzer, VarExpr &expr)
{
    using Type = SemanticAnalyzer::Type;

    // Special case: NOTHING keyword represents a null pointer.
    // It's parsed as VarExpr{"NOTHING"} and lowering emits Value::null().
    if (expr.name == "NOTHING")
    {
        return Type::Unknown; // Null pointer has Unknown type in BASIC semantics
    }

    ExprCheckContext context(analyzer);
    context.resolveAndTrackSymbolRef(expr.name);

    if (!context.hasSymbol(expr.name))
    {
        // Find closest matching symbol for suggestion using Levenshtein distance
        std::string best;
        std::size_t bestDist = std::numeric_limits<std::size_t>::max();
        for (const auto &s : context.symbols())
        {
            std::size_t d = semantic_analyzer_detail::levenshtein(expr.name, s);
            if (d < bestDist)
            {
                bestDist = d;
                best = s;
            }
        }

        std::string suggestion;
        if (!best.empty())
        {
            suggestion = "; did you mean '" + best + "'?";
        }

        context.diagnostics().emit(
            diag::BasicDiag::UnknownVariable,
            expr.loc,
            static_cast<uint32_t>(expr.name.size()),
            std::initializer_list<diag::Replacement>{diag::Replacement{"name", expr.name},
                                                     diag::Replacement{"suggestion", suggestion}});
        return Type::Unknown;
    }

    // Check for explicit type declaration
    auto varTy = context.varType(expr.name);
    if (varTy)
        return *varTy;

    // Apply BASIC suffix rules for implicit types
    if (!expr.name.empty())
    {
        if (expr.name.back() == '$')
            return Type::String;
        if (expr.name.back() == '#' || expr.name.back() == '!')
            return Type::Float;
    }

    return Type::Int;
}

} // namespace il::frontends::basic::sem
