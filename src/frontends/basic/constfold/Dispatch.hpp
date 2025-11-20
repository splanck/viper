//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the public entry points for BASIC constant folding.  The dispatcher
// maps AST operations to specialised helpers based on operand kinds to keep the
// main folder lightweight.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Constant folding dispatcher API for the BASIC front end.
/// @details Exposes helpers that allow expression visitors to query whether an
///          expression is foldable and to obtain the folded literal when
///          possible.  Domain-specific implementations live in the neighbouring
///          translation units.

#pragma once

#include "frontends/basic/ast/ExprNodes.hpp"

#include <optional>
#include <string>

namespace il::frontends::basic::constfold
{

namespace AST
{
using ::il::frontends::basic::BinaryExpr;
using ::il::frontends::basic::BoolExpr;
using ::il::frontends::basic::Expr;
using ::il::frontends::basic::ExprPtr;
using ::il::frontends::basic::UnaryExpr;
} // namespace AST

/// @brief Numeric literal representation shared by folding helpers.
struct NumericValue
{
    bool isFloat = false;
    double f = 0.0;
    long long i = 0;
};

/// @brief Literal categories understood by the dispatcher.
enum class LiteralKind
{
    Int,
    Float,
    Bool,
    String,
    Invalid,
};

/// @brief Result container emitted by folding helpers.
struct Constant
{
    LiteralKind kind = LiteralKind::Invalid;
    NumericValue numeric{};
    bool boolValue = false;
    std::string stringValue;
};

/// @brief Domains handled by the constant-fold dispatcher.
enum class FoldKind
{
    Arith,
    Logical,
    Compare,
    Strings,
    Casts,
};

/// @brief Convert an expression into its numeric payload when possible.
std::optional<NumericValue> numeric_from_expr(const AST::Expr &expr);

/// @brief Promote @p lhs to match @p rhs according to BASIC rules.
NumericValue promote_numeric(const NumericValue &lhs, const NumericValue &rhs);

/// @brief Fold unary arithmetic operators (Plus/Negate) on literals.
AST::ExprPtr fold_unary_arith(AST::UnaryExpr::Op op, const AST::Expr &value);

/// @brief Fold logical NOT when applied to literal operands.
AST::ExprPtr fold_logical_not(const AST::Expr &operand);

/// @brief Inspect whether a short-circuit logical operator terminates early.
std::optional<bool> try_short_circuit(AST::BinaryExpr::Op op, const AST::BoolExpr &lhs);

/// @brief Determine if @p op performs short-circuit evaluation.
bool is_short_circuit(AST::BinaryExpr::Op op);

/// @brief Fold boolean binary expressions with literal operands.
AST::ExprPtr fold_boolean_binary(const AST::Expr &lhs,
                                 AST::BinaryExpr::Op op,
                                 const AST::Expr &rhs);

/// @brief Fold LEN builtin when the argument is a literal.
AST::ExprPtr foldLenLiteral(const AST::Expr &arg);

/// @brief Fold MID$ builtin when all operands are literals.
AST::ExprPtr foldMidLiteral(const AST::Expr &source,
                            const AST::Expr &start,
                            const AST::Expr &length);

/// @brief Fold LEFT$ builtin when both operands are literals.
AST::ExprPtr foldLeftLiteral(const AST::Expr &source, const AST::Expr &count);

/// @brief Fold RIGHT$ builtin when both operands are literals.
AST::ExprPtr foldRightLiteral(const AST::Expr &source, const AST::Expr &count);

/// @brief Fold CHR$ builtin when the argument is a literal integer.
AST::ExprPtr foldChrLiteral(const AST::Expr &arg);

/// @brief Check whether @p expr can be folded to a literal value.
bool can_fold(const AST::Expr &expr);

/// @brief Attempt to fold @p expr into a freshly allocated literal node.
std::optional<AST::ExprPtr> fold_expr(const AST::Expr &expr);

} // namespace il::frontends::basic::constfold
