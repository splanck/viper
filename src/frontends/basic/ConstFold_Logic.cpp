// File: src/frontends/basic/ConstFold_Logic.cpp
// Purpose: Implements logical constant folding utilities for BASIC expressions.
// Key invariants: Helpers respect BASIC short-circuit semantics and preserve
//                 boolean typing for folded expressions.
// Ownership/Lifetime: Returned expressions are heap-allocated and owned by
//                     callers.
// Links: docs/codemap.md

#include "frontends/basic/ConstFold_Logic.hpp"

#include "frontends/basic/ConstFolder.hpp"

#include <utility>

namespace il::frontends::basic::detail
{
namespace
{
ExprPtr makeBool(bool value)
{
    auto expr = std::make_unique<BoolExpr>();
    expr->value = value;
    return expr;
}

ExprPtr makeInt(long long value)
{
    auto expr = std::make_unique<IntExpr>();
    expr->value = value;
    return expr;
}
} // namespace

ExprPtr foldLogicalNot(const Expr &operand)
{
    if (auto *boolExpr = dynamic_cast<const BoolExpr *>(&operand))
        return makeBool(!boolExpr->value);

    if (auto numeric = asNumeric(operand))
    {
        if (numeric->isFloat)
            return nullptr;
        return makeInt(numeric->i == 0 ? 1 : 0);
    }

    return nullptr;
}

std::optional<bool> tryShortCircuit(BinaryExpr::Op op, const BoolExpr &lhs)
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAndShort:
            if (!lhs.value)
                return false;
            break;
        case BinaryExpr::Op::LogicalOrShort:
            if (lhs.value)
                return true;
            break;
        default:
            break;
    }
    return std::nullopt;
}

bool isShortCircuitOp(BinaryExpr::Op op)
{
    return op == BinaryExpr::Op::LogicalAndShort || op == BinaryExpr::Op::LogicalOrShort;
}

ExprPtr foldLogicalBinary(const Expr &lhs, BinaryExpr::Op op, const Expr &rhs)
{
    auto *lhsBool = dynamic_cast<const BoolExpr *>(&lhs);
    auto *rhsBool = dynamic_cast<const BoolExpr *>(&rhs);
    if (!lhsBool || !rhsBool)
        return nullptr;

    switch (op)
    {
        case BinaryExpr::Op::LogicalAnd:
        case BinaryExpr::Op::LogicalAndShort:
            return makeBool(lhsBool->value && rhsBool->value);
        case BinaryExpr::Op::LogicalOr:
        case BinaryExpr::Op::LogicalOrShort:
            return makeBool(lhsBool->value || rhsBool->value);
        default:
            return nullptr;
    }
}

} // namespace il::frontends::basic::detail
