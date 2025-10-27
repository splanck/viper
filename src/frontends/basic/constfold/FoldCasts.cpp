//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Cast-related constant folding helpers.  The BASIC binary grammar does not
// currently expose explicit cast operators, so the dispatcher defers to this
// file for completeness.  The helpers simply report that no folding is
// available, keeping the table-driven structure uniform.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/constfold/Dispatch.hpp"

namespace il::frontends::basic::constfold::detail
{
std::optional<Constant> fold_casts(BinaryExpr::Op, const Constant &, const Constant &)
{
    return std::nullopt;
}

} // namespace il::frontends::basic::constfold::detail
