//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/PeepholePass.hpp
// Purpose: Declare the peephole optimisation pass for the AArch64 codegen pipeline.
// Key invariants: Must run after RegAllocPass (operates on physical-register MIR).
// Ownership/Lifetime: Stateless pass; mutates AArch64Module::mir in place.
// Links: docs/codemap.md, src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes {

/// @brief Apply peephole optimisations to all MIR functions after register allocation.
class PeepholePass final : public Pass {
  public:
    /// @brief Controls which subset of peephole patterns to apply.
    enum class Mode {
        Full,               ///< Apply all peephole patterns (default).
        PostScheduleCleanup ///< Apply only lightweight cleanup after scheduling.
    };

    /// @param mode Which pattern set to use; defaults to Mode::Full.
    explicit PeepholePass(Mode mode = Mode::Full) noexcept : mode_(mode) {}

    /// @brief Run peephole optimisations on all functions in AArch64Module::mir.
    /// @param module Module state; mir must have physical registers assigned.
    /// @param diags  Diagnostic sink (peephole is non-failing; always returns true).
    /// @return Always true; peephole failures are ignored silently.
    bool run(AArch64Module &module, Diagnostics &diags) override;

  private:
    Mode mode_{Mode::Full};
};

} // namespace viper::codegen::aarch64::passes
