//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/LegalizePass.hpp
// Purpose: Declare the AArch64 MIR legalization pass.
// Key invariants: Runs after LoweringPass and before RegAllocPass. All
//                 overflow pseudos and backend-required entry sequences must
//                 be expanded before register allocation and emission.
// Ownership/Lifetime: Stateless pass; mutates AArch64Module::mir in place.
// Links: docs/internals/codemap.md, src/codegen/aarch64/LowerOvf.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace zanna::codegen::aarch64::passes {

/// @brief Normalize lowered AArch64 MIR before register allocation.
class LegalizePass final : public Pass {
  public:
    /// @brief Expand pseudos, insert required runtime entry calls, and refresh
    ///        function call/leaf metadata.
    /// @param module Module state; mir and ti must be populated.
    /// @param diags  Diagnostic sink for legalization errors.
    /// @return True when legalization succeeds.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace zanna::codegen::aarch64::passes
