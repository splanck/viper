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

} // namespace detail

} // namespace il::frontends::basic
