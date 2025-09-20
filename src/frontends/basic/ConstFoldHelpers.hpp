// File: src/frontends/basic/ConstFoldHelpers.hpp
// Purpose: Generic helpers for ConstFolder numeric, comparison, and string operations.
// Key invariants: Helpers rely on Numeric promotion semantics and preserve 64-bit wrap-around for
// integers. Ownership/Lifetime: Returned ExprPtr objects are heap-allocated and owned by the
// caller. Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/ConstFolder.hpp"
#include <optional>
#include <utility>

namespace il::frontends::basic::detail
{
/// @brief Apply arithmetic operation on two literals with promotion.
/// @param l Left operand expression.
/// @param r Right operand expression.
/// @param fop Operation to apply when either operand is float.
/// @param iop Operation to apply when both operands are integers.
/// @return Folded literal or nullptr on mismatch.
/// @invariant Integer operation must model 64-bit wrap-around semantics when needed.
template <typename FloatOp, typename IntOp>
ExprPtr foldArithmetic(const Expr &l, const Expr &r, FloatOp fop, IntOp iop);

/// @brief Apply comparison or logical operation on two literals with promotion.
/// @param l Left operand expression.
/// @param r Right operand expression.
/// @param fcmp Comparator when either operand is float.
/// @param icmp Comparator when both operands are integers.
/// @param allowFloat Whether float operands are permitted (false causes failure if any is float).
/// @return Integer literal 0/1 or nullptr on mismatch.
/// @invariant Result is always integer; 1 for true, 0 for false.
template <typename FloatCmp, typename IntCmp>
ExprPtr foldCompare(
    const Expr &l, const Expr &r, FloatCmp fcmp, IntCmp icmp, bool allowFloat = true);

/// @brief Apply binary string operation using callback @p op.
/// @param l Left string operand.
/// @param r Right string operand.
/// @param op Callback operating on string values and returning ExprPtr.
/// @return Folded literal produced by @p op.
/// @invariant Caller ensures @p op models BASIC semantics.
template <typename Op> ExprPtr foldString(const StringExpr &l, const StringExpr &r, Op op);

} // namespace il::frontends::basic::detail
