//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Arithmetic constant folding helpers.  These routines evaluate numeric
// operations in isolation so the dispatcher can remain a light-weight table
// lookup keyed on operator and operand type.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/constfold/Dispatch.hpp"

#include "frontends/basic/ConstFold_Arith.hpp"

#include <cassert>

namespace il::frontends::basic::constfold::detail
{
namespace
{
using ::il::frontends::basic::detail::Numeric;
using ::il::frontends::basic::detail::tryFoldBinaryArith;

[[nodiscard]] bool isNumeric(const Constant &c)
{
    return c.type == LiteralType::Int || c.type == LiteralType::Float;
}

[[nodiscard]] Numeric toNumeric(const Constant &c)
{
    return Numeric{c.isFloat, c.isFloat ? c.floatValue : static_cast<double>(c.intValue), c.intValue};
}

[[nodiscard]] Constant fromNumeric(const Numeric &numeric)
{
    return numeric.isFloat ? makeFloatConstant(numeric.f) : makeIntConstant(numeric.i);
}

#ifdef VIPER_CONSTFOLD_ASSERTS
void checkCommutative(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs, const Constant &result)
{
    if (op != BinaryExpr::Op::Add && op != BinaryExpr::Op::Mul)
        return;
    if (!isNumeric(lhs) || !isNumeric(rhs))
        return;
    if (lhs.isFloat || rhs.isFloat)
        return;

    auto swapped = tryFoldBinaryArith(toNumeric(rhs), op, toNumeric(lhs));
    if (!swapped)
        return;
    Constant swappedResult = fromNumeric(*swapped);
    assert(swappedResult.type == result.type);
    assert(swappedResult.intValue == result.intValue);
}
#endif

bool supports(BinaryExpr::Op op)
{
    switch (op)
    {
        case BinaryExpr::Op::Add:
        case BinaryExpr::Op::Sub:
        case BinaryExpr::Op::Mul:
        case BinaryExpr::Op::Div:
        case BinaryExpr::Op::IDiv:
        case BinaryExpr::Op::Mod:
            return true;
        default:
            return false;
    }
}
} // namespace

std::optional<Constant> fold_arith(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs)
{
    if (!supports(op))
        return std::nullopt;
    if (!isNumeric(lhs) || !isNumeric(rhs))
        return std::nullopt;

    auto folded = tryFoldBinaryArith(toNumeric(lhs), op, toNumeric(rhs));
    if (!folded)
        return std::nullopt;

    Constant result = fromNumeric(*folded);

#ifdef VIPER_CONSTFOLD_ASSERTS
    checkCommutative(op, lhs, rhs, result);
#endif

    return result;
}

} // namespace il::frontends::basic::constfold::detail
