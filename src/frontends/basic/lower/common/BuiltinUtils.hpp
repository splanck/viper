//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/common/BuiltinUtils.hpp
// Purpose: Declare builtin dispatch helpers shared across BASIC lowering.
// Key invariants: Dispatch tables remain deterministic and initialise exactly
//                 once across the process lifetime.
// Ownership/Lifetime: Relies on process-wide builtin registry entries and does
//                     not allocate persistent state beyond handler bindings.
// Links: docs/basic-language.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

namespace il::frontends::basic::lower::common
{
/// @brief Lower a BASIC builtin call by dispatching through the handler registry.
[[nodiscard]] Lowerer::RVal lowerBuiltinCall(Lowerer &lowerer, const BuiltinCallExpr &call);

/// @brief Initialise builtin handler registry; exposed for unit testing.
void ensureBuiltinHandlersForTesting();

} // namespace il::frontends::basic::lower::common
