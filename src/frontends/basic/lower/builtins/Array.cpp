//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Array-oriented BASIC builtins are currently lowered elsewhere in the pipeline
// using bespoke passes.  This translation unit therefore provides the
// intentionally empty registrar used by the BASIC front end to hook in lowering
// code for each builtin family.  Keeping the stub in-tree documents the
// extension point for array-specific lowering logic, preserves the invariant
// that every builtin domain exports a registrar, and makes it obvious where
// future work should live once the lowering rules expand.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Registrar stub for BASIC array builtin lowering.
/// @details Exposes @ref registerArrayBuiltins so the shared builtin registry
///          remains uniform even though array-specific lowerings have not been
///          implemented yet.

#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{
/// @brief Install array builtin lowering rules into the shared registry.
/// @details The lowering pipeline invokes a registrar for every builtin domain
///          during initialisation.  Array intrinsics currently lower through
///          generic expression handling, so there is nothing to register yet.
///          We nevertheless keep this hook so that:
///            - Higher level code can unconditionally call every registrar
///              without guarding for feature availability.
///            - Tooling that inspects the registry sees a slot for array
///              builtins and can surface TODOs accordingly.
///            - Future developers have a documented entry point when specialised
///              array lowering becomes necessary.
///          The body intentionally remains a no-op.
void registerArrayBuiltins()
{
    // No array-specific builtin lowering is routed through the shared registry.
}
} // namespace il::frontends::basic::lower::builtins
