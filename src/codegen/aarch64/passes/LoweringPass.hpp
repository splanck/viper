//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/LoweringPass.hpp
// Purpose: Declare the IL-to-MIR lowering pass for the AArch64 codegen pipeline.
// Key invariants: Populates AArch64Module::mir from AArch64Module::ilMod.
//                 ilMod and ti must be non-null before this pass runs.
// Ownership/Lifetime: Stateless pass; mutates the provided AArch64Module in place.
// Links: docs/codemap.md, src/codegen/aarch64/LowerILToMIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes
{

/// @brief Lower all IL functions in AArch64Module::ilMod to MIR, storing
///        results in AArch64Module::mir.
class LoweringPass final : public Pass
{
  public:
    /// @brief Run the lowering pass for all functions in the IL module.
    /// @param module Module state; ilMod and ti must be non-null.
    /// @param diags  Diagnostic sink for lowering errors.
    /// @return True if all functions were lowered successfully.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::aarch64::passes
