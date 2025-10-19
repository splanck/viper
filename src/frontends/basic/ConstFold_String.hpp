// File: src/frontends/basic/ConstFold_String.hpp
// Purpose: Declares string constant folding utilities for BASIC expressions.
// Key invariants: Helpers honor BASIC slicing semantics, clamp to valid bounds,
//                 and avoid evaluating non-literal operands.
// Ownership/Lifetime: Returned expressions are heap-allocated and owned by
//                     callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/ExprNodes.hpp"

namespace il::frontends::basic::detail
{
/// @brief Fold string concatenation when both operands are literals.
ExprPtr foldStringConcat(const StringExpr &l, const StringExpr &r);

/// @brief Fold string concatenation for arbitrary literal expressions.
/// @param lhs Left operand inspected for string literal content.
/// @param rhs Right operand inspected for string literal content.
/// @return Concatenated literal or nullptr when folding is not possible.
ExprPtr foldStringBinaryConcat(const Expr &lhs, const Expr &rhs);

/// @brief Fold string equality comparison when both operands are literals.
ExprPtr foldStringEq(const StringExpr &l, const StringExpr &r);

/// @brief Fold string inequality comparison when both operands are literals.
ExprPtr foldStringNe(const StringExpr &l, const StringExpr &r);

/// @brief Fold string equality comparison for arbitrary literal expressions.
/// @param lhs Left operand inspected for string literal content.
/// @param rhs Right operand inspected for string literal content.
/// @return Integer literal encoding equality or nullptr when folding fails.
ExprPtr foldStringBinaryEq(const Expr &lhs, const Expr &rhs);

/// @brief Fold string inequality comparison for arbitrary literal expressions.
/// @param lhs Left operand inspected for string literal content.
/// @param rhs Right operand inspected for string literal content.
/// @return Integer literal encoding inequality or nullptr when folding fails.
ExprPtr foldStringBinaryNe(const Expr &lhs, const Expr &rhs);

/// @brief Fold LEN when invoked on a literal string argument.
/// @param arg Expression supplying the string operand.
/// @return Integer literal with the string length or nullptr when folding fails.
ExprPtr foldLenLiteral(const Expr &arg);

/// @brief Fold MID$ on literal string with literal bounds.
/// @param source Expression containing the base string literal.
/// @param startExpr Expression supplying the one-based start index.
/// @param lengthExpr Expression supplying the slice length.
/// @return String literal slice or nullptr when folding fails.
ExprPtr foldMidLiteral(const Expr &source, const Expr &startExpr, const Expr &lengthExpr);

/// @brief Fold LEFT$ on literal string with literal count.
/// @param source Expression containing the base string literal.
/// @param countExpr Expression supplying the requested prefix length.
/// @return String literal prefix or nullptr when folding fails.
ExprPtr foldLeftLiteral(const Expr &source, const Expr &countExpr);

/// @brief Fold RIGHT$ on literal string with literal count.
/// @param source Expression containing the base string literal.
/// @param countExpr Expression supplying the requested suffix length.
/// @return String literal suffix or nullptr when folding fails.
ExprPtr foldRightLiteral(const Expr &source, const Expr &countExpr);

} // namespace il::frontends::basic::detail
