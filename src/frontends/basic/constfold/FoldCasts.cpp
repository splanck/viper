//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Cast-related constant folding helpers for the BASIC front end.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides stubs for cast folding.  BASIC currently performs casts via
///        builtins that are lowered elsewhere, so the dispatcher exposes a hook
///        for future use.

#include "frontends/basic/constfold/Dispatch.hpp"

namespace il::frontends::basic::constfold
{

std::optional<Constant> fold_cast(AST::BinaryExpr::Op, const Constant &, const Constant &)
{
    return std::nullopt;
}

} // namespace il::frontends::basic::constfold
