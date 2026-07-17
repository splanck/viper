//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/PreRegAllocOptPass.hpp
// Purpose: Pass-manager wrapper for AArch64 pre-register-allocation MIR cleanup.
// Key invariants:
//   - Must run after LegalizePass and before RegAllocPass.
//   - Delegates to PreRegAllocOpt free functions; always returns true.
// Ownership/Lifetime: Stateless pass; mutates AArch64Module::mir in place.
// Links: codegen/aarch64/passes/PreRegAllocOptPass.cpp,
//        codegen/aarch64/PreRegAllocOpt.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace zanna::codegen::aarch64::passes {

/// @brief Run pre-RA MIR cleanup optimisations (copy coalescing, dead code, etc.).
class PreRegAllocOptPass final : public Pass {
  public:
    /// @brief Run the pre-RA optimisation pipeline on all MIR functions.
    /// @param module Module state; mir must have been lowered and legalized.
    /// @param diags  Diagnostic sink (non-failing; always returns true).
    /// @return Always true.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace zanna::codegen::aarch64::passes
