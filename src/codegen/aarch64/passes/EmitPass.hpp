//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/EmitPass.hpp
// Purpose: Declare the assembly emission pass for the AArch64 codegen pipeline.
// Key invariants: Requires AArch64Module::mir to have physical registers assigned.
//                 Populates AArch64Module::assembly with the final asm text.
// Ownership/Lifetime: Stateless pass; mutates AArch64Module::assembly.
// Links: docs/codemap.md, src/codegen/aarch64/AsmEmitter.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes
{

/// @brief Emit AArch64 assembly text from all MIR functions.
class EmitPass final : public Pass
{
  public:
    /// @brief Run the emission pass: generate assembly text into AArch64Module::assembly.
    /// @param module Module state; mir must have physical registers assigned.
    /// @param diags  Diagnostic sink for emission errors.
    /// @return True if emission succeeded for all functions.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::aarch64::passes
