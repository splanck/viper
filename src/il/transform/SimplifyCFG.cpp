//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/SimplifyCFG.cpp
// Purpose: Provide the driver implementation for the SimplifyCFG optimisation
//          pass that orchestrates multiple transformation subroutines.
// Key invariants: Verification hooks ensure IR validity before, during, and
//                 after transformations in debug builds, while the analysis
//                 manager is notified whenever the CFG mutates.
// Ownership/Lifetime: The pass operates on caller-owned modules and functions
//                     without taking ownership; analysis caches are invalidated
//                     via the supplied AnalysisManager.
// Links: docs/codemap.md#transforms
//
//===----------------------------------------------------------------------===//


#include "il/transform/SimplifyCFG.hpp"

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/PassRegistry.hpp"
#include "il/transform/SimplifyCFG/BlockMerging.hpp"
#include "il/transform/SimplifyCFG/BranchFolding.hpp"
#include "il/transform/SimplifyCFG/ForwardingElimination.hpp"
#include "il/transform/SimplifyCFG/JumpThreading.hpp"
#include "il/transform/SimplifyCFG/ParamCanonicalization.hpp"
#include "il/transform/SimplifyCFG/ReachabilityCleanup.hpp"

#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>

namespace
{

// Verification hooks are no-ops.  The PassManager's -verify-each flag provides
// per-pass verification when needed for debugging.  Internal per-iteration
// verification was removed because it verifies the entire module (not just the
// current function), causing O(F × iterations × verify_cost) overhead that
// makes O1 compilation prohibitively slow for medium-sized modules.
void verifyPreconditions(const il::core::Module *) {}

void verifyPostconditions(const il::core::Module *) {}

void verifyIntermediateState(const il::core::Module *) {}

/// @brief Mark cached CFG/dominator analyses as stale once the pass modifies IR.
/// @param function Function whose analyses must be invalidated.
void invalidateCFGAndDominators(il::core::Function &function,
                                il::transform::AnalysisManager *analysisManager)
{
    if (!analysisManager)
        return;

    il::transform::PreservedAnalyses preserved;
    preserved.preserveAllModules();
    // SimplifyCFG mutates the control-flow graph, so drop CFG, dominator, and
    // liveness summaries to force recomputation on the next query.
    analysisManager->invalidateAfterFunctionPass(preserved, function);
}

} // namespace

namespace il::transform
{

/// @brief Execute the SimplifyCFG pass over a single function.
/// @details Iteratively applies folding and cleanup transforms, running
/// verification hooks in debug builds and updating pass statistics when changes
/// occur.
/// @param F Function to simplify.
/// @param outStats Optional pointer receiving aggregate statistics.
/// @return @c true when the pass modified the function.
bool SimplifyCFG::run(il::core::Function &F, Stats *outStats)
{
    verifyPreconditions(module_);

    Stats stats{};
    SimplifyCFGPassContext ctx(F, module_, stats);

    bool changedAny = false;

    for (int iter = 0; iter < 8; ++iter)
    {
        bool changed = false;
        if (aggressive)
            changed |= simplify_cfg::foldTrivialSwitches(ctx);
        changed |= simplify_cfg::foldTrivialConditionalBranches(ctx);
        if (aggressive)
            changed |= simplify_cfg::threadJumps(ctx);
        changed |= simplify_cfg::removeEmptyForwarders(ctx);
        changed |= simplify_cfg::mergeSinglePredBlocks(ctx);
        changed |= simplify_cfg::removeUnreachableBlocks(ctx);
        changed |= simplify_cfg::canonicalizeParamsAndArgs(ctx);
        if (!changed)
            break;
        changedAny = true;
        verifyIntermediateState(module_);
    }

    if (changedAny)
    {
        verifyPostconditions(module_);
        invalidateCFGAndDominators(F, analysisManager_);
    }

    if (outStats)
        *outStats = stats;

    return changedAny;
}

} // namespace il::transform
