//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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
#include "il/transform/SimplifyCFG/ParamCanonicalization.hpp"
#include "il/transform/SimplifyCFG/ReachabilityCleanup.hpp"

#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>

namespace
{

#ifndef NDEBUG
/// @brief Optionally verify the module before running the pass (debug builds).
/// @param module Owning module when available.
void verifyPreconditions(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG precondition verification failed");
    (void)verified;
}

/// @brief Optionally verify the module after the pass completes (debug builds).
/// @param module Owning module when available.
void verifyPostconditions(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG postcondition verification failed");
    (void)verified;
}

/// @brief Optionally verify the module between transformation iterations.
/// @param module Owning module when available.
void verifyIntermediateState(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG verification failed after transformation batch");
    (void)verified;
}
#else
/// @brief No-op verification hook used in release builds.
/// @param module Ignored module pointer kept for signature parity.
void verifyPreconditions(const il::core::Module *module)
{
    (void)module;
}

/// @brief No-op verification hook used in release builds after the pass.
/// @param module Ignored module pointer kept for signature parity.
void verifyPostconditions(const il::core::Module *module)
{
    (void)module;
}

/// @brief No-op verification hook used in release builds between iterations.
/// @param module Ignored module pointer kept for signature parity.
void verifyIntermediateState(const il::core::Module *module)
{
    (void)module;
}
#endif

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
