//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/SchedulerPass.hpp
// Purpose: Declare the x86-64 post-RA instruction scheduling pass.
// Key invariants:
//   - Runs after register allocation on physical-register MIR.
//   - Skipped when optimizeLevel < 1.
// Ownership/Lifetime:
//   - Stateless pass; mutates Module::mir in place.
// Links: codegen/x86_64/passes/SchedulerPass.cpp,
//        codegen/x86_64/passes/PassManager.hpp,
//        codegen/x86_64/Scheduler.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes {

/// @brief Post-RA instruction scheduling pass for the x86-64 codegen pipeline.
class SchedulerPass final : public Pass {
  public:
    /// @brief Run the scheduling pass on post-allocation MIR.
    /// @param module Backend pipeline state containing physical-register MIR.
    /// @param diags  Diagnostic sink for ordering errors.
    /// @return @c true when scheduling succeeds or is skipped.
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::x64::passes

