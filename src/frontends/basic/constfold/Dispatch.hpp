//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Declares the domain-dispatch interface for BASIC constant folding.  Callers
// probe the dispatcher to determine whether an expression can be folded and to
// retrieve the folded literal when possible.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "frontends/basic/ast/ExprNodes.hpp"

namespace AST = il::frontends::basic;

namespace il::frontends::basic::constfold
{

/// @brief Logical grouping for constant folding routines.
enum class FoldKind
{
    Arith,
    Logical,
    Compare,
    Strings,
    Casts,
};

/// @brief Determine whether @p e is eligible for constant folding.
[[nodiscard]] bool can_fold(const AST::Expr &e);

/// @brief Fold expression @p e into a literal when possible.
/// @return Newly allocated literal expression or empty optional when folding fails.
[[nodiscard]] std::optional<AST::ExprPtr> fold_expr(const AST::Expr &e);

namespace detail
{

/// @brief Literal categories handled by the dispatcher.
enum class LiteralType
{
    Int,
    Float,
    Bool,
    String,
    Numeric, ///< Wildcard for numeric operands.
};

/// @brief In-memory representation of a literal value.
struct Constant
{
    LiteralType type{LiteralType::Int};
    bool isFloat{false};
    double floatValue{0.0};
    long long intValue{0};
    bool boolValue{false};
    std::string stringValue;
};

/// @brief Construct an integer constant wrapper.
inline Constant makeIntConstant(long long value)
{
    Constant c;
    c.type = LiteralType::Int;
    c.intValue = value;
    c.boolValue = value != 0;
    return c;
}

/// @brief Construct a floating-point constant wrapper.
inline Constant makeFloatConstant(double value)
{
    Constant c;
    c.type = LiteralType::Float;
    c.isFloat = true;
    c.floatValue = value;
    c.intValue = static_cast<long long>(value);
    return c;
}

/// @brief Construct a boolean constant wrapper.
inline Constant makeBoolConstant(bool value)
{
    Constant c;
    c.type = LiteralType::Bool;
    c.boolValue = value;
    c.intValue = value ? 1 : 0;
    return c;
}

/// @brief Construct a string constant wrapper.
inline Constant makeStringConstant(std::string value)
{
    Constant c;
    c.type = LiteralType::String;
    c.stringValue = std::move(value);
    return c;
}

std::optional<Constant> fold_arith(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs);
std::optional<Constant> fold_logical(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs);
std::optional<Constant> fold_compare(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs);
std::optional<Constant> fold_strings(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs);
std::optional<Constant> fold_casts(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs);

} // namespace detail

} // namespace il::frontends::basic::constfold
