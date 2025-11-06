//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Thin translation unit that anchors the shared builtin lowering interfaces and
// pulls in the family-specific registrars.  The heavy lifting lives under
// src/frontends/basic/lower/builtins/ where each domain provides its own
// implementation and registration logic.  Consolidating the anchor in a
// dedicated file keeps link dependencies predictable for embedders that only
// reference a subset of the lowering pipeline.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/BuiltinCommon.hpp"
#include "frontends/basic/lower/builtins/Registrars.hpp"

/// @file
/// @brief Anchor point for shared BASIC builtin lowering registrations.
/// @details Including this translation unit ensures that domain-specific
///          registrars are linked into any binary that references the
///          `builtin` lowering facilities.  The file intentionally avoids
///          additional behaviour so the linker can discard unused builtin
///          families when whole-program optimisation is enabled.

namespace il::frontends::basic::lower
{
namespace builtins
{
/// @brief Anchor function that forces builtin registrars to be linked.
/// @details The lowering pipeline registers builtin handlers through static
///          initialisation performed in domain-specific translation units.  Some
///          build configurations (notably when using dead-stripping linkers)
///          discard those objects unless a symbol from the TU is referenced.
///          Calling or even odr-using this function keeps the object file alive
///          so all builtin families remain available to the lowering pass.
void anchorBuiltinLowering()
{
    // Intentionally empty.
}
} // namespace builtins
} // namespace il::frontends::basic::lower
