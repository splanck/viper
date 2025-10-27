//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/LegalizePass.cpp
// Purpose: Implement the legalisation pass guard in the x86-64 codegen pipeline.
// Key invariants: Legalisation is only considered successful when lowering has produced an
//                 adapter module; future extensions can add real transformations here.
// Ownership/Lifetime: Stateless pass mutating Module flags in place.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Legalisation gate for the x86-64 backend pipeline.
/// @details Currently models the successful completion of Phase A lowering by
///          checking for the presence of an adapter module. The abstraction
///          makes it trivial to grow into a real legaliser while keeping the
///          pass sequencing logic unchanged.

#include "codegen/x86_64/passes/LegalizePass.hpp"

namespace viper::codegen::x64::passes
{

/// @brief Verify that lowering produced an adapter module before flagging legalisation.
/// @details Emits a descriptive diagnostic when lowering has not populated the
///          module's adapter artefact. On success, sets @c legalised so later
///          passes (register allocation, emission) know they can assume lowered
///          IR exists.
/// @param module Backend pipeline state being mutated.
/// @param diags  Diagnostics sink used to report ordering problems.
/// @return @c true when legalisation conditions are satisfied.
bool LegalizePass::run(Module &module, Diagnostics &diags)
{
    if (!module.lowered)
    {
        diags.error("legalize: lowering has not produced an adapter module");
        return false;
    }
    module.legalised = true;
    return true;
}

} // namespace viper::codegen::x64::passes

