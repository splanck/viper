//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/RegAllocLinear.cpp
// Purpose: Tie together the linear-scan register allocation pipeline that turns
//          Machine IR with virtual registers into a form annotated with
//          physical register assignments and spill slots.
// Key invariants: Phases execute in a deterministic order — live interval
//                 analysis is computed prior to allocation so every virtual
//                 register has a defined lifetime, the allocator consumes the
//                 immutable analysis snapshot, and the summary returned from the
//                 pass precisely describes spill usage for later lowering
//                 passes.
// Ownership/Lifetime: Mutates the supplied Machine IR in place, retaining
//                     ownership of the function with the caller while the
//                     AllocationResult returned by this translation unit is a
//                     lightweight summary consumed by downstream passes.
// Links: src/codegen/x86_64/ra/LiveIntervals.hpp,
//        src/codegen/x86_64/ra/Allocator.hpp,
//        docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point that orchestrates linear-scan register allocation.
/// @details Exposes @ref allocate, the façade that threads together live
///          interval analysis and the allocator implementation so callers can
///          request register assignment for a machine function with a single
///          call.

#include "RegAllocLinear.hpp"

#include "ra/Allocator.hpp"
#include "ra/LiveIntervals.hpp"

namespace viper::codegen::x64
{

/// @brief Run the linear-scan register allocator over a function.
/// @details The orchestration follows three clear steps:
///          1. Instantiate @ref ra::LiveIntervals and run it across @p func to
///             compute lifetime ranges for every virtual register.  The pass
///             mutates the analysis object but leaves the function unchanged.
///          2. Construct @ref ra::LinearScanAllocator with the computed
///             intervals, the machine function, and the target description so it
///             can interpret architectural register classes correctly.
///          3. Invoke @ref ra::LinearScanAllocator::run to perform allocation,
///             spill insertion, and coalescing.  The allocator applies updates
///             directly to @p func and returns an @ref AllocationResult that
///             summarises the chosen assignments and spill slot utilisation for
///             subsequent passes.
///          Keeping the sequencing here means callers do not need to understand
///          the interplay between analyses and transformations when requesting a
///          register allocation.
/// @param func Machine function to allocate in place.
/// @param target Target lowering information describing available registers and
///               register classes.
/// @return Summary of the allocation, including spill slot usage.
AllocationResult allocate(MFunction &func, const TargetInfo &target)
{
    ra::LiveIntervals intervals{};
    intervals.run(func);

    ra::LinearScanAllocator allocator{func, target, intervals};
    return allocator.run();
}

} // namespace viper::codegen::x64
