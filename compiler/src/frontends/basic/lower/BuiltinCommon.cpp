//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Anchor translation unit for the shared BASIC builtin lowering interfaces.
// The heavy lifting lives under src/frontends/basic/lower/builtins/ where each
// domain (numeric, runtime, string, etc.) defines its own registration logic.
// Keeping this anchor isolated prevents build systems from discarding the
// builtin machinery when the only references come from static registration
// blocks, and documents the canonical place to extend the registry.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Shared infrastructure for BASIC builtin lowering registrars.
/// @details Declares @ref builtins::anchorBuiltinLowering so callers can force
///          the translation unit to link, ensuring that domain-specific
///          registrar constructors execute.

#include "frontends/basic/lower/BuiltinCommon.hpp"
#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower
{
namespace builtins
{
/// @brief Anchor symbol that forces the builtin lowering translation unit to link.
/// @details The builtin lowering registry is populated via static initialisers
///          inside the domain-specific registrar files.  Some linkers discard
///          object files that contain only such side effects unless a concrete
///          symbol is referenced.  By calling this no-op routine from higher
///          level code, we guarantee the translation unit remains part of the
///          final binary and therefore that all registrar constructors run.
///          Beyond that linkage guarantee the function intentionally performs no
///          work, keeping call sites side-effect free.
void anchorBuiltinLowering()
{
    // Intentionally empty.
}
} // namespace builtins
} // namespace il::frontends::basic::lower
