//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Array-oriented BASIC builtins are currently lowered elsewhere in the pipeline.
// This translation unit exists so future array helpers have a dedicated home and
// to keep the builtin registrar structure uniform across domains.  Maintaining
// the stub ensures downstream tooling can rely on every builtin family exposing
// a registrar even when no special lowering is required yet.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{
/// @brief Placeholder registrar for array builtins.
/// @details The BASIC front-end does not currently lower any array builtins via
///          the shared registry.  Installing an explicit no-op registrar keeps
///          the builtin registration pattern consistent and signals where future
///          lowering code should live.
void registerArrayBuiltins()
{
    // No array-specific builtin lowering is routed through the shared registry.
}
} // namespace il::frontends::basic::lower::builtins
