//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/RegAllocPass.hpp
// Purpose: Declare the register allocation pass for the AArch64 codegen pipeline.
// Key invariants: Requires AArch64Module::mir to be populated by LoweringPass.
//                 Assigns physical registers to all virtual registers in-place.
// Ownership/Lifetime: Stateless pass; mutates AArch64Module::mir in place.
// Links: docs/codemap.md, src/codegen/aarch64/RegAllocLinear.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes
{

/// @brief Run linear-scan register allocation on all MIR functions.
class RegAllocPass final : public Pass
{
  public:
    /// @brief Run register allocation on all functions in AArch64Module::mir.
    /// @param module Module state; mir must be populated.
    /// @param diags  Diagnostic sink for allocation errors.
    /// @return True if allocation succeeded for all functions.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::aarch64::passes
