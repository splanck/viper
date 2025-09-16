// File: src/frontends/basic/ConstFolder.hpp
// Purpose: Declares utilities to fold constant BASIC expressions.
// Key invariants: Only pure expressions with literal operands are folded.
// Ownership/Lifetime: Functions mutate AST in place, nodes owned by caller.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
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

ExprPtr foldNumericAdd(const Expr &l, const Expr &r);
ExprPtr foldNumericSub(const Expr &l, const Expr &r);
ExprPtr foldNumericMul(const Expr &l, const Expr &r);
ExprPtr foldNumericDiv(const Expr &l, const Expr &r);
ExprPtr foldNumericIDiv(const Expr &l, const Expr &r);
ExprPtr foldNumericMod(const Expr &l, const Expr &r);
ExprPtr foldNumericEq(const Expr &l, const Expr &r);
ExprPtr foldNumericNe(const Expr &l, const Expr &r);
ExprPtr foldNumericLt(const Expr &l, const Expr &r);
ExprPtr foldNumericLe(const Expr &l, const Expr &r);
ExprPtr foldNumericGt(const Expr &l, const Expr &r);
ExprPtr foldNumericGe(const Expr &l, const Expr &r);
ExprPtr foldNumericAnd(const Expr &l, const Expr &r);
ExprPtr foldNumericOr(const Expr &l, const Expr &r);

ExprPtr foldStringConcat(const StringExpr &l, const StringExpr &r);
ExprPtr foldStringEq(const StringExpr &l, const StringExpr &r);
ExprPtr foldStringNe(const StringExpr &l, const StringExpr &r);

inline constexpr std::array<BinaryFoldEntry, 14> kBinaryFoldTable = {{
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
    {BinaryExpr::Op::And, &foldNumericAnd, nullptr},
    {BinaryExpr::Op::Or, &foldNumericOr, nullptr},
}};

inline const BinaryFoldEntry *findBinaryFold(BinaryExpr::Op op)
{
    for (const auto &entry : kBinaryFoldTable)
    {
        if (entry.op == op)
            return &entry;
    }
    return nullptr;
}
} // namespace detail

} // namespace il::frontends::basic
