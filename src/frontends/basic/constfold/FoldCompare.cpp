//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Comparison constant folding helpers.  These routines normalise numeric and
// string comparisons to integer truth values to mirror BASIC semantics.
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
using ::il::frontends::basic::detail::tryFoldCompare;

[[nodiscard]] bool isNumeric(const Constant &c)
{
    return c.type == LiteralType::Int || c.type == LiteralType::Float;
}

[[nodiscard]] Numeric toNumeric(const Constant &c)
{
    return Numeric{c.isFloat, c.isFloat ? c.floatValue : static_cast<double>(c.intValue), c.intValue};
}

[[nodiscard]] bool supports(BinaryExpr::Op op)
{
    switch (op)
    {
        case BinaryExpr::Op::Eq:
        case BinaryExpr::Op::Ne:
        case BinaryExpr::Op::Lt:
        case BinaryExpr::Op::Le:
        case BinaryExpr::Op::Gt:
        case BinaryExpr::Op::Ge:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] Constant fromNumeric(const Numeric &numeric)
{
    return makeIntConstant(numeric.i);
}
} // namespace

std::optional<Constant> fold_compare(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs)
{
    if (!supports(op))
        return std::nullopt;

    if (lhs.type == LiteralType::String && rhs.type == LiteralType::String)
    {
        if (op != BinaryExpr::Op::Eq && op != BinaryExpr::Op::Ne)
            return std::nullopt;
        bool equal = lhs.stringValue == rhs.stringValue;
        bool value = (op == BinaryExpr::Op::Eq) ? equal : !equal;
#ifdef VIPER_CONSTFOLD_ASSERTS
        bool swappedEqual = rhs.stringValue == lhs.stringValue;
        assert(swappedEqual == equal);
#endif
        return makeIntConstant(value ? 1 : 0);
    }

    if (!isNumeric(lhs) || !isNumeric(rhs))
        return std::nullopt;

    auto folded = tryFoldCompare(toNumeric(lhs), op, toNumeric(rhs));
    if (!folded)
        return std::nullopt;

    Constant result = fromNumeric(*folded);

#ifdef VIPER_CONSTFOLD_ASSERTS
    auto swapped = tryFoldCompare(toNumeric(rhs), op, toNumeric(lhs));
    if (swapped)
        assert(swapped->i == folded->i);
#endif

    return result;
}

} // namespace il::frontends::basic::constfold::detail
