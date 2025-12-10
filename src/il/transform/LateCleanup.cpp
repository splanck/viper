//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the LateCleanup pass for late-pipeline optimization cleanup.
// The pass combines SimplifyCFG and DCE in a bounded fixpoint to efficiently
// remove dead code and simplify control flow created by earlier optimization
// passes.
//
//===----------------------------------------------------------------------===//

#include "il/transform/LateCleanup.hpp"

#include "il/transform/DCE.hpp"
#include "il/transform/SimplifyCFG.hpp"

#include "il/core/Module.hpp"

#include <cstddef>

using namespace il::core;

namespace il::transform
{

namespace
{
std::size_t countInstructions(const Module &module)
{
    std::size_t total = 0;
    for (const auto &fn : module.functions)
        for (const auto &block : fn.blocks)
            total += block.instructions.size();
    return total;
}

std::size_t countBlocks(const Module &module)
{
    std::size_t total = 0;
    for (const auto &fn : module.functions)
        total += fn.blocks.size();
    return total;
}
} // namespace

std::string_view LateCleanup::id() const
{
    return "late-cleanup";
}

PreservedAnalyses LateCleanup::run(Module &module, AnalysisManager &analysis)
{
    bool changedAny = false;

    constexpr unsigned kMaxIterations = 4;

    const std::size_t initialInstr = countInstructions(module);
    const std::size_t initialBlocks = countBlocks(module);
    std::size_t currentInstr = initialInstr;
    std::size_t currentBlocks = initialBlocks;

    if (stats_)
    {
        stats_->instrBefore = initialInstr;
        stats_->blocksBefore = initialBlocks;
    }

    for (unsigned iter = 0; iter < kMaxIterations; ++iter)
    {
        const std::size_t iterStartInstr = currentInstr;
        const std::size_t iterStartBlocks = currentBlocks;
        bool simplifyChanged = false;

        // Run SimplifyCFG on each function
        for (auto &function : module.functions)
        {
            SimplifyCFG cfgPass(/*aggressive=*/true);
            cfgPass.setModule(&module);
            cfgPass.setAnalysisManager(&analysis);

            SimplifyCFG::Stats stats;
            simplifyChanged |= cfgPass.run(function, &stats);
        }

        // Run DCE on the entire module
        dce(module);

        currentInstr = countInstructions(module);
        currentBlocks = countBlocks(module);

        const bool sizeChanged = currentInstr != iterStartInstr || currentBlocks != iterStartBlocks;
        const bool iterChanged = simplifyChanged || sizeChanged;

        if (stats_)
        {
            stats_->instrPerIter.push_back(currentInstr);
            stats_->blocksPerIter.push_back(currentBlocks);
        }

        changedAny |= iterChanged;
        if (!iterChanged)
            break;
    }

    if (stats_)
    {
        stats_->iterations = static_cast<unsigned>(stats_->instrPerIter.size());
        stats_->instrAfter = currentInstr;
        stats_->blocksAfter = currentBlocks;
    }

    if (!changedAny)
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
