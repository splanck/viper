//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// String constant folding helpers.  Concatenation is handled here so callers can
// request folding without re-encoding BASIC slicing logic.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/constfold/Dispatch.hpp"

namespace il::frontends::basic::constfold::detail
{
namespace
{
[[nodiscard]] bool supports(BinaryExpr::Op op)
{
    return op == BinaryExpr::Op::Add;
}
} // namespace

std::optional<Constant> fold_strings(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs)
{
    if (!supports(op))
        return std::nullopt;
    if (lhs.type != LiteralType::String || rhs.type != LiteralType::String)
        return std::nullopt;
    return makeStringConstant(lhs.stringValue + rhs.stringValue);
}

} // namespace il::frontends::basic::constfold::detail
