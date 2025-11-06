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
// domain exposes a single entry point.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{
/// @brief Placeholder registrar for I/O builtins.
/// @details No file I/O builtins currently route through the shared registry, so
///          this function intentionally performs no work while documenting where
///          additional registration logic should land when the need arises.
void registerIoBuiltins()
{
    // No I/O-specific builtin lowering is installed via the shared registry yet.
}
} // namespace il::frontends::basic::lower::builtins
