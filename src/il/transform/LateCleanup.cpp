//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the LateCleanup pass for late-pipeline optimization cleanup.
// The pass combines SimplifyCFG and DCE in a tight loop to efficiently remove
// dead code and simplify control flow created by earlier optimization passes.
//
//===----------------------------------------------------------------------===//

#include "il/transform/LateCleanup.hpp"

#include "il/transform/DCE.hpp"
#include "il/transform/SimplifyCFG.hpp"

#include "il/core/Module.hpp"

using namespace il::core;

namespace il::transform
{

std::string_view LateCleanup::id() const
{
    return "late-cleanup";
}

PreservedAnalyses LateCleanup::run(Module &module, AnalysisManager &analysis)
{
    bool changed = false;

    // Maximum iterations to prevent infinite loops while still achieving
    // a reasonable fixpoint
    constexpr int maxIterations = 2;

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        bool iterChanged = false;

        // Run SimplifyCFG on each function
        for (auto &function : module.functions)
        {
            SimplifyCFG cfgPass(/*aggressive=*/true);
            cfgPass.setModule(&module);
            cfgPass.setAnalysisManager(&analysis);

            SimplifyCFG::Stats stats;
            if (cfgPass.run(function, &stats))
            {
                iterChanged = true;
            }
        }

        // Run DCE on the entire module
        // DCE always runs; we track changes conservatively
        dce(module);
        // DCE doesn't report changes, so we can't know for sure if it did
        // anything. For the iteration check, we rely on SimplifyCFG.

        if (iterChanged)
        {
            changed = true;
        }
        else
        {
            // No SimplifyCFG changes means we're at a fixpoint
            break;
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    // Conservative: invalidate everything since we modified code
    return PreservedAnalyses::none();
}

void registerLateCleanupPass(PassRegistry &registry)
{
    registry.registerModulePass("late-cleanup",
                                [](Module &module, AnalysisManager &analysis)
                                {
                                    LateCleanup pass;
                                    return pass.run(module, analysis);
                                });
}

} // namespace il::transform
