//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File I/O builtins are handled by specialised lowering paths outside the
// generic registry.  The registrar remains so future extensions can hook into
// the dispatcher without modifying unrelated families.  Retaining this stub
// keeps the registration surface uniform so tooling can assume each builtin
// domain exposes a single entry point.  The file therefore documents the
// intentional absence of shared I/O lowering today and provides a place to add
// it when the runtime grows new capabilities.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Registrar stub for BASIC file I/O builtin lowering.
/// @details Provides @ref registerIoBuiltins to keep the builtin registry shape
///          consistent even though file I/O lowering is handled by bespoke
///          routines elsewhere in the frontend.

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/BuiltinCommon.hpp"
#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{

namespace
{
/// @brief Lower TIMER builtin using the generic rule-driven path.
Lowerer::RVal lowerTimerBuiltin(BuiltinLowerContext &ctx)
{
    return lower::lowerGenericBuiltin(ctx);
}
} // namespace

/// @brief Placeholder registrar for BASIC file I/O builtins.
/// @details File I/O builtins interact with runtime channel state, so the BASIC
///          front end routes them through bespoke lowering routines today.
///          Nevertheless the shared registry expects every domain to expose a
///          registrar.  Calling this function registers any I/O-related builtins
///          that can use the generic lowering path.
void registerIoBuiltins()
{
    using Builtin = BuiltinCallExpr::Builtin;
    // Register TIMER builtin to use the generic lowering path
    register_builtin(getBuiltinInfo(Builtin::Timer).name, &lowerTimerBuiltin);
}
} // namespace il::frontends::basic::lower::builtins
