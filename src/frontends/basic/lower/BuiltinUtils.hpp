//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Declares the entry point for lowering BASIC builtin calls via the handler
// registry populated at static initialisation time.  The dispatcher relies on
// the shared utilities defined in BuiltinCommon to implement the rule-driven
// emission paths for each builtin family.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

namespace il::frontends::basic::lower
{
/// @brief Lower a BASIC builtin call by dispatching through the handler registry.
[[nodiscard]] Lowerer::RVal lowerBuiltinCall(Lowerer &lowerer, const BuiltinCallExpr &call);

} // namespace il::frontends::basic::lower
