// File: src/frontends/basic/ConstFolder.hpp
// Purpose: Declares utilities to fold constant BASIC expressions.
// Key invariants: Only pure expressions with literal operands are folded.
// Ownership/Lifetime: Functions mutate AST in place, nodes owned by caller.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/Token.hpp"
#include <optional>

namespace il::frontends::basic
{

/// @brief Numeric value that may be an int or float.
struct Numeric
{
    bool isFloat; ///< True if @c f is valid.
    double f;     ///< Floating point value.
    long long i;  ///< Integer value.
};

/// @brief Attempt to view @p e as a numeric literal.
/// @param e Expression to inspect.
/// @return Numeric value if @p e is IntExpr or FloatExpr.
std::optional<Numeric> asNumeric(const Expr &e);

/// @brief Promote @p a to match type of @p b (float if either is float).
Numeric promote(const Numeric &a, const Numeric &b);

/// @brief Fold numeric binary expression @p L op @p R with operation @p op.
template <typename F> ExprPtr foldNumericBinary(const Expr &L, const Expr &R, F op);

/// @brief Fold string binary operation if supported.
ExprPtr foldStringBinary(const StringExpr &L, TokenKind op, const StringExpr &R);

/// \brief Fold constant expressions within a BASIC program AST.
/// \param prog Program to transform in place.
void foldConstants(Program &prog);

} // namespace il::frontends::basic
