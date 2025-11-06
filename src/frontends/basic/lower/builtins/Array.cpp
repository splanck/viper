//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Array-oriented BASIC builtins are currently lowered elsewhere in the pipeline.
// This translation unit provides the intentionally empty registrar used by the
// BASIC front end to hook in lowering code for each builtin family.  Keeping the
// stub in-tree documents the extension point for array-specific lowering logic,
// preserves the invariant that every builtin domain exports a registrar, and
// makes it obvious where future work should live once the lowering rules grow.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{
/// @brief Install array builtin lowering rules into the shared registry.
/// @details The BASIC lowering pipeline wires builtin support together by
///          invoking a registrar function for each feature domain (numeric,
///          runtime, string, array, ...).  Array intrinsics do not yet require
///          bespoke lowering, so this registrar deliberately performs no
///          registration while still advertising the canonical entry point.  The
///          no-op keeps the call sites uniform, simplifies feature detection in
///          downstream tools, and serves as a breadcrumb for future lowering
///          additions.
void registerArrayBuiltins()
{
    // No array-specific builtin lowering is routed through the shared registry.
}
} // namespace il::frontends::basic::lower::builtins
