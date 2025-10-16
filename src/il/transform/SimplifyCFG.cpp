//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the scaffold for the control-flow graph simplification pass. The
// actual simplification logic is organised across helper modules that perform
// individual transformations (branch folding, parameter canonicalisation, etc.).
// This file wires the helpers together and maintains verification invariants.
//
//===----------------------------------------------------------------------===//

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
void verifyPreconditions(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG precondition verification failed");
    (void)verified;
}

void verifyPostconditions(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG postcondition verification failed");
    (void)verified;
}

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

void invalidateCFGAndDominators(il::core::Function &function)
{
    static_cast<void>(function);
    // TODO: Hook into analysis invalidation once caches are connected.
}

} // namespace

namespace il::transform
{

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

