//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File I/O builtins are handled by specialised lowering paths outside the
// generic registry.  The registrar remains so future extensions can hook into
// the dispatcher without modifying unrelated families.  Retaining this stub also
// keeps the registration surface uniform so tooling can assume each builtin
// domain exposes a single entry point and that the linker pulls in every
// builtin family consistently.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/builtins/Registrars.hpp"

/// @file
/// @brief Placeholder registrar for BASIC file I/O intrinsic lowering.
/// @details File-system intrinsics require bespoke lowering to handle runtime
///          permissions and host integration.  Until that work lands, the stub
///          registrar provides a documented extension point that mirrors the
///          structure of other builtin families, preventing surprises when the
///          registry enumerates available domains.

namespace il::frontends::basic::lower::builtins
{
/// @brief Stub registrar for file I/O builtins awaiting dedicated lowering.
/// @details The shared builtin registry expects each domain to expose a
///          registrar even when no handlers are currently installed.  Leaving the
///          body empty preserves binary compatibility while signalling where
///          future lowering code should connect when BASIC's file intrinsics are
///          implemented.
void registerIoBuiltins()
{
    // No I/O-specific builtin lowering is installed via the shared registry yet.
}
} // namespace il::frontends::basic::lower::builtins
