//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/RegAllocPass.cpp
// Purpose: Implement the register allocation gating pass for the x86-64 pipeline.
// Key invariants: Register allocation is only considered complete when legalisation has run.
// Ownership/Lifetime: Stateless transformation toggling Module flags.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Register allocation guard pass for the x86-64 code generator.
/// @details Defines the thin pass wrapper that validates pipeline ordering
///          prior to flagging register allocation as finished. The guard is
///          intentionally conservative to make it obvious which prerequisite
///          step was skipped when the pass fails.

#include "codegen/x86_64/passes/RegAllocPass.hpp"

namespace viper::codegen::x64::passes
{

/// @brief Ensure legalisation ran before marking the module as register-allocated.
/// @details The pass checks the @p module bookkeeping flags and emits a
///          descriptive diagnostic when legalisation has not yet succeeded. On
///          success it flips the `registersAllocated` flag, allowing subsequent
///          passes (e.g., emission) to proceed.
/// @param module Backend pipeline state to update.
/// @param diags  Diagnostics sink for reporting ordering mistakes.
/// @return @c true when register allocation can be considered complete.
bool RegAllocPass::run(Module &module, Diagnostics &diags)
{
    if (!module.legalised)
    {
        diags.error("regalloc: legalisation must run before register allocation");
        return false;
    }
    module.registersAllocated = true;
    return true;
}

} // namespace viper::codegen::x64::passes
