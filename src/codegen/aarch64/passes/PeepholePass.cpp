//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/PeepholePass.cpp
// Purpose: Peephole optimisation pass for the AArch64 modular pipeline.
//
// Runs the AArch64 peephole optimizer on each MIR function after register
// allocation.  Peephole failures are silently ignored — the pass always
// returns true (non-fatal).
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/PeepholePass.hpp"

#include "codegen/aarch64/Peephole.hpp"

namespace viper::codegen::aarch64::passes
{

bool PeepholePass::run(AArch64Module &module, Diagnostics & /*diags*/)
{
    for (auto &fn : module.mir)
    {
        [[maybe_unused]] auto stats = runPeephole(fn);
        pruneUnusedCalleeSaved(fn);
    }
    return true;
}

} // namespace viper::codegen::aarch64::passes
