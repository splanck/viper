//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File I/O builtins are handled by specialised lowering paths outside the
// generic registry.  The registrar remains so future extensions can hook into
// the dispatcher without modifying unrelated families.  Retaining this stub also
// keeps the registration surface uniform so tooling can assume each builtin
// domain exposes a single entry point.  The file therefore documents the
// intentional absence of shared I/O lowering and provides a place to add it when
// the runtime grows new capabilities.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{
/// @brief Placeholder registrar for BASIC file I/O builtins.
/// @details The shared lowering registry expects each builtin domain to expose a
///          registration hook.  At present, file I/O routines are lowered by
///          bespoke code because they interact directly with runtime handles and
///          require dedicated diagnostics.  This function intentionally performs
///          no work but keeps the registry contract satisfied, ensuring future
///          additions have a well-defined entry point.
void registerIoBuiltins()
{
    // No I/O-specific builtin lowering is installed via the shared registry yet.
}
} // namespace il::frontends::basic::lower::builtins
