//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
/// @details Looks up the builtin identifier captured on @p call, ensures the corresponding
///          handler has been registered, and invokes it using the provided @p lowerer.  The
///          returned @ref Lowerer::RVal wraps the IL value produced by the handler.  When a
///          builtin is missing, the helper emits a lowering diagnostic before yielding an empty
///          result.
[[nodiscard]] Lowerer::RVal lowerBuiltinCall(Lowerer &lowerer, const BuiltinCallExpr &call);

/// @brief Ensure builtin handlers are registered for unit tests.
/// @details Lazily initialises the registry to the default production state so tests can exercise
///          lowering routines without depending on the global initialization side effects
///          performed by the full driver.
void ensureBuiltinHandlersForTesting();

} // namespace il::frontends::basic::lower::common
