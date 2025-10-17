//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements the scaffold for the control-flow graph simplification pass.
/// @details Coordinates helper modules that perform individual transformations
/// (branch folding, parameter canonicalisation, etc.) and maintains verification
/// invariants around each batch of changes.


#include "il/transform/SimplifyCFG.hpp"

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
void verifyPreconditions(const il::core::Module *) {}

void verifyPostconditions(const il::core::Module *) {}

void verifyIntermediateState(const il::core::Module *) {}
#endif

/// @brief Mark cached CFG/dominator analyses as stale once the pass modifies IR.
/// @param function Function whose analyses must be invalidated.
void invalidateCFGAndDominators(il::core::Function &function)
{
    static_cast<void>(function);
    // TODO: Hook into analysis invalidation once caches are connected.
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
        invalidateCFGAndDominators(F);
    }

    if (outStats)
        *outStats = stats;

    return changedAny;
}

} // namespace il::transform

