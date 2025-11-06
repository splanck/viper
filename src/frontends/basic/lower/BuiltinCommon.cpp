//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Thin translation unit that anchors the shared builtin lowering interfaces and
// pulls in the family-specific registrars.  The heavy lifting lives under
// src/frontends/basic/lower/builtins/ where each domain provides its own
// implementation and registration logic.  Keeping the anchor isolated ensures
// build systems do not accidentally drop the builtin lowering machinery when no
// domain-specific file references it directly.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/BuiltinCommon.hpp"
#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower
{
namespace builtins
{
/// @brief Anchor symbol that forces the builtin lowering translation unit to link.
/// @details Some build configurations only pull in object files when they expose
///          referenced symbols.  Because the builtin lowering registrars rely on
///          static constructors and registration side effects, this empty helper
///          provides a symbol that higher level code can reference to guarantee
///          the translation unit is part of the final binary.  The function has
///          no behavioural impact beyond preserving linkage.
void anchorBuiltinLowering()
{
    // Intentionally empty.
}
} // namespace builtins
} // namespace il::frontends::basic::lower
