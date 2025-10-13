//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the scaffold for the control-flow graph simplification pass. The
// actual simplification logic will be filled in by future work; for now, the
// pass exposes a run method that records default statistics and advertises that
// no mutations take place.
//
//===----------------------------------------------------------------------===//

#include "il/transform/SimplifyCFG.hpp"

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
#else
void verifyPreconditions(const il::core::Module *) {}
#endif

} // namespace

namespace il::transform
{

bool SimplifyCFG::run(il::core::Function &F, Stats *outStats)
{
    verifyPreconditions(module_);

    Stats stats{};
    bool changed = false;

    (void)F;
    // TODO: Implement CFG simplifications covering branch folding, block merging,
    // and unreachable elimination once the design is finalised.

    if (outStats)
        *outStats = stats;

    return changed;
}

} // namespace il::transform
