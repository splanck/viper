//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Logical constant folding helpers focused on numeric truthiness operators.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/constfold/Dispatch.hpp"

#include <cassert>

namespace il::frontends::basic::constfold::detail
{
namespace
{
[[nodiscard]] bool isIntegralLike(const Constant &c)
{
    return c.type == LiteralType::Int || c.type == LiteralType::Bool;
}

[[nodiscard]] bool eval(BinaryExpr::Op op, bool lhs, bool rhs)
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAnd:
        case BinaryExpr::Op::LogicalAndShort:
            return lhs && rhs;
        case BinaryExpr::Op::LogicalOr:
        case BinaryExpr::Op::LogicalOrShort:
            return lhs || rhs;
        default:
            return false;
    }
}

[[nodiscard]] bool supports(BinaryExpr::Op op)
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAnd:
        case BinaryExpr::Op::LogicalAndShort:
        case BinaryExpr::Op::LogicalOr:
        case BinaryExpr::Op::LogicalOrShort:
            return true;
        default:
            return false;
    }
}
} // namespace

std::optional<Constant> fold_logical(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs)
{
    if (!supports(op))
        return std::nullopt;
    if (!isIntegralLike(lhs) || !isIntegralLike(rhs))
        return std::nullopt;
    if (lhs.isFloat || rhs.isFloat)
        return std::nullopt;

    bool left = lhs.intValue != 0;
    bool right = rhs.intValue != 0;
    bool value = eval(op, left, right);

#ifdef VIPER_CONSTFOLD_ASSERTS
    bool swapped = eval(op, right, left);
    assert(swapped == value);
#endif

    return makeIntConstant(value ? 1 : 0);
}

} // namespace il::frontends::basic::constfold::detail
