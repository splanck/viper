// File: src/frontends/basic/ConstFold_Arith.hpp
// Purpose: Declares arithmetic constant folding utilities for BASIC expressions.
// Key invariants: Helpers respect BASIC numeric promotion and 64-bit wrap semantics.
// Ownership/Lifetime: Returned expressions are heap-allocated and owned by callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ConstFolder.hpp"

namespace il::frontends::basic::detail
{

/// @brief Fold arithmetic binary operations when both operands are literals.
/// @param l Left operand expression.
/// @param op Binary operator to evaluate.
/// @param r Right operand expression.
/// @return Folded literal or nullptr on mismatch.
ExprPtr foldBinaryArith(const Expr &l, BinaryExpr::Op op, const Expr &r);

/// @brief Fold arithmetic unary operations when operand is a literal.
/// @param op Unary operator to apply.
/// @param v Operand expression interpreted as numeric literal.
/// @return Folded literal or nullptr on mismatch.
ExprPtr foldUnaryArith(UnaryExpr::Op op, const Expr &v);

/// @brief Fold numeric comparisons producing integer truth values.
/// @param l Left operand expression.
/// @param op Comparison operator.
/// @param r Right operand expression.
/// @param allowFloat Whether floating point operands are permitted.
/// @return Folded literal or nullptr on mismatch.
ExprPtr foldCompare(const Expr &l, BinaryExpr::Op op, const Expr &r, bool allowFloat = true);

/// @brief Attempt to fold arithmetic binary operation on numeric literals.
/// @param lhs Left numeric value.
/// @param op Binary operator.
/// @param rhs Right numeric value.
/// @return Numeric literal result or std::nullopt if folding is not possible.
std::optional<Numeric> tryFoldBinaryArith(const Numeric &lhs,
                                          BinaryExpr::Op op,
                                          const Numeric &rhs);

/// @brief Attempt to fold arithmetic unary operation on numeric literal.
/// @param op Unary operator.
/// @param value Operand numeric value.
/// @return Numeric literal result or std::nullopt when unsupported.
std::optional<Numeric> tryFoldUnaryArith(UnaryExpr::Op op, const Numeric &value);

/// @brief Attempt to fold comparison of numeric literals.
/// @param lhs Left numeric value.
/// @param op Comparison operator.
/// @param rhs Right numeric value.
/// @param allowFloat Whether floating point operands are permitted.
/// @return Integer-valued numeric literal result or std::nullopt on mismatch.
std::optional<Numeric> tryFoldCompare(const Numeric &lhs,
                                      BinaryExpr::Op op,
                                      const Numeric &rhs,
                                      bool allowFloat = true);

} // namespace il::frontends::basic::detail
