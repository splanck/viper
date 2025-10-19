// File: src/frontends/basic/ConstFolder.hpp
// Purpose: Declares utilities to fold constant BASIC expressions.
// Key invariants: Only pure expressions with literal operands are folded.
// Ownership/Lifetime: Functions mutate AST in place, nodes owned by caller.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ConstFold_String.hpp"
#include "frontends/basic/Token.hpp"
#include <array>
#include <optional>

namespace il::frontends::basic
{

/// \brief Fold constant expressions within a BASIC program AST.
/// \param prog Program to transform in place.
void foldConstants(Program &prog);

namespace detail
{
/// @brief Numeric literal wrapper used during folding.
struct Numeric
{
    bool isFloat; ///< True if the value is floating point.
    double f;     ///< Floating-point representation.
    long long i;  ///< Integer representation.
};

/// @brief Interpret expression @p e as a numeric literal if possible.
std::optional<Numeric> asNumeric(const Expr &e);

/// @brief Promote @p a to match the type of @p b.
Numeric promote(const Numeric &a, const Numeric &b);

/// @brief Fold numeric binary operation using callback @p op.
/// @return Folded expression or nullptr on mismatch.
template <typename F> ExprPtr foldNumericBinary(const Expr &l, const Expr &r, F op);

/// @brief Function pointer type for numeric binary fold handlers.
using BinaryNumericFn = ExprPtr (*)(const Expr &, const Expr &);

/// @brief Function pointer type for string binary fold handlers.
using BinaryStringFn = ExprPtr (*)(const StringExpr &, const StringExpr &);

/// @brief Table entry describing how to fold a BASIC binary operator.
struct BinaryFoldEntry
{
    BinaryExpr::Op op;      ///< AST opcode represented by the entry.
    BinaryNumericFn numeric; ///< Numeric evaluator (nullptr when unsupported).
    BinaryStringFn string;   ///< String evaluator (nullptr when unsupported).
};

/// @brief Fold numeric addition for literal operands.
/// @details Uses foldArithmetic with lambdas for floating addition and wrapAdd for
/// integers.
ExprPtr foldNumericAdd(const Expr &l, const Expr &r);

/// @brief Fold numeric subtraction for literal operands.
/// @details Uses foldArithmetic with lambdas for floating subtraction and wrapSub for
/// integers.
ExprPtr foldNumericSub(const Expr &l, const Expr &r);

/// @brief Fold numeric multiplication for literal operands.
/// @details Uses foldArithmetic with lambdas for floating multiplication and wrapMul for
/// integers.
ExprPtr foldNumericMul(const Expr &l, const Expr &r);

/// @brief Fold numeric division for literal operands.
/// @details Uses foldNumericBinary with a lambda that rejects division by zero and
/// promotes floats.
ExprPtr foldNumericDiv(const Expr &l, const Expr &r);

/// @brief Fold integer division for literal operands.
/// @details Uses foldNumericBinary with a lambda that rejects floats and zero
/// divisors.
ExprPtr foldNumericIDiv(const Expr &l, const Expr &r);

/// @brief Fold integer modulus for literal operands.
/// @details Uses foldNumericBinary with a lambda that rejects floats and zero
/// divisors.
ExprPtr foldNumericMod(const Expr &l, const Expr &r);

/// @brief Fold numeric equality comparison for literal operands.
/// @details Uses foldCompare with lambdas for floating and integer equality
/// checks.
ExprPtr foldNumericEq(const Expr &l, const Expr &r);

/// @brief Fold numeric inequality comparison for literal operands.
/// @details Uses foldCompare with lambdas for floating and integer inequality
/// checks.
ExprPtr foldNumericNe(const Expr &l, const Expr &r);

/// @brief Fold numeric less-than comparison for literal operands.
/// @details Uses foldCompare with lambdas that implement floating and integer
/// ordering.
ExprPtr foldNumericLt(const Expr &l, const Expr &r);

/// @brief Fold numeric less-or-equal comparison for literal operands.
/// @details Uses foldCompare with lambdas that implement floating and integer
/// ordering.
ExprPtr foldNumericLe(const Expr &l, const Expr &r);

/// @brief Fold numeric greater-than comparison for literal operands.
/// @details Uses foldCompare with lambdas that implement floating and integer
/// ordering.
ExprPtr foldNumericGt(const Expr &l, const Expr &r);

/// @brief Fold numeric greater-or-equal comparison for literal operands.
/// @details Uses foldCompare with lambdas that implement floating and integer
/// ordering.
ExprPtr foldNumericGe(const Expr &l, const Expr &r);

/// @brief Fold numeric logical AND for literal operands.
/// @details Uses foldCompare with a lambda pair that enforces integer-only
/// truthiness.
ExprPtr foldNumericAnd(const Expr &l, const Expr &r);

/// @brief Fold numeric logical OR for literal operands.
/// @details Uses foldCompare with a lambda pair that enforces integer-only
/// truthiness.
ExprPtr foldNumericOr(const Expr &l, const Expr &r);

inline constexpr std::array<BinaryFoldEntry, 16> kBinaryFoldTable = {{
    {BinaryExpr::Op::Add, &foldNumericAdd, &foldStringConcat},
    {BinaryExpr::Op::Sub, &foldNumericSub, nullptr},
    {BinaryExpr::Op::Mul, &foldNumericMul, nullptr},
    {BinaryExpr::Op::Div, &foldNumericDiv, nullptr},
    {BinaryExpr::Op::IDiv, &foldNumericIDiv, nullptr},
    {BinaryExpr::Op::Mod, &foldNumericMod, nullptr},
    {BinaryExpr::Op::Eq, &foldNumericEq, &foldStringEq},
    {BinaryExpr::Op::Ne, &foldNumericNe, &foldStringNe},
    {BinaryExpr::Op::Lt, &foldNumericLt, nullptr},
    {BinaryExpr::Op::Le, &foldNumericLe, nullptr},
    {BinaryExpr::Op::Gt, &foldNumericGt, nullptr},
    {BinaryExpr::Op::Ge, &foldNumericGe, nullptr},
    {BinaryExpr::Op::LogicalAndShort, &foldNumericAnd, nullptr},
    {BinaryExpr::Op::LogicalOrShort, &foldNumericOr, nullptr},
    {BinaryExpr::Op::LogicalAnd, &foldNumericAnd, nullptr},
    {BinaryExpr::Op::LogicalOr, &foldNumericOr, nullptr},
}};

/// @brief Look up folding handlers for a BASIC binary operator.
/// @return Pointer to the table entry or nullptr when the operator is unsupported.
const BinaryFoldEntry *findBinaryFold(BinaryExpr::Op op);
} // namespace detail

} // namespace il::frontends::basic
