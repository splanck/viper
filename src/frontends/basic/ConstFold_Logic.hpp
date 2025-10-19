// File: src/frontends/basic/ConstFold_Logic.hpp
// Purpose: Declares logical constant folding utilities for BASIC expressions.
// Key invariants: Folding preserves boolean short-circuit semantics and does not
//                 evaluate operands when BASIC would avoid them.
// Ownership/Lifetime: Returned expressions are heap-allocated and owned by
//                     callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/ExprNodes.hpp"

#include <optional>

namespace il::frontends::basic::detail
{
/// @brief Fold logical NOT when applied to literal operands.
/// @param operand Expression inspected for folding.
/// @return Replacement expression or nullptr if folding is not possible.
ExprPtr foldLogicalNot(const Expr &operand);

/// @brief Compute short-circuit value for boolean operands when available.
/// @param op Logical binary operator under evaluation.
/// @param lhs Left operand, assumed to be a BoolExpr.
/// @return Boolean value when evaluation short-circuits; std::nullopt otherwise.
std::optional<bool> tryShortCircuit(BinaryExpr::Op op, const BoolExpr &lhs);

/// @brief Check whether @p op is a short-circuit logical operator.
/// @param op Binary operator to inspect.
/// @return True when @p op performs short-circuit evaluation.
bool isShortCircuitOp(BinaryExpr::Op op);

/// @brief Fold boolean binary operations when both operands are BoolExpr
/// literals.
/// @param lhs Left operand expression.
/// @param op Logical operator to evaluate.
/// @param rhs Right operand expression.
/// @return Replacement BoolExpr or nullptr when folding is not possible.
ExprPtr foldLogicalBinary(const Expr &lhs, BinaryExpr::Op op, const Expr &rhs);

} // namespace il::frontends::basic::detail
