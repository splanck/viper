//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File I/O builtins are handled by specialised lowering paths outside the
// generic registry.  The registrar remains so future extensions can hook into
// the dispatcher without modifying unrelated families.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{
void registerIoBuiltins()
{
    // No I/O-specific builtin lowering is installed via the shared registry yet.
}
} // namespace il::frontends::basic::lower::builtins
