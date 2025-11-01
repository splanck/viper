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
// implementation and registration logic.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/BuiltinCommon.hpp"
#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower
{
namespace builtins
{
/// @brief No-op helper whose sole purpose is to ensure this translation unit is
///        linked when the builtin lowering support is referenced.  Domain
///        registrars reside in their dedicated files.
void anchorBuiltinLowering()
{
    // Intentionally empty.
}
} // namespace builtins
} // namespace il::frontends::basic::lower
