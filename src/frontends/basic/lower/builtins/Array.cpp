//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Array-oriented BASIC builtins are currently lowered elsewhere in the pipeline.
// This translation unit exists so future array helpers have a dedicated home and
// to keep the builtin registrar structure uniform across domains.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/builtins/Registrars.hpp"

namespace il::frontends::basic::lower::builtins
{
void registerArrayBuiltins()
{
    // No array-specific builtin lowering is routed through the shared registry.
}
} // namespace il::frontends::basic::lower::builtins
