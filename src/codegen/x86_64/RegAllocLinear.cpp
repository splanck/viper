//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/RegAllocLinear.cpp
// Purpose: Provide the top-level entry point for the linear-scan register
//          allocator by wiring together individual analysis and transformation
//          phases.
// Key invariants: Phases execute in a deterministic order — live interval
//                 analysis followed by allocation, spill insertion, and
//                 coalescing.
// Ownership/Lifetime: Mutates the supplied Machine IR in place and returns an
//                     allocation summary consumed by later codegen passes while
//                     leaving ownership of the MIR with the caller.
// Links: src/codegen/x86_64/ra/LiveIntervals.hpp,
//        src/codegen/x86_64/ra/Allocator.hpp,
//        docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

#include "RegAllocLinear.hpp"

#include "ra/Allocator.hpp"
#include "ra/LiveIntervals.hpp"

namespace viper::codegen::x64
{

/// @brief Run the linear-scan register allocator over a function.
/// @details Computes live intervals for every virtual register, feeds them into
///          the @ref ra::LinearScanAllocator, and returns the resulting
///          allocation summary.  The helper owns the sequencing of analyses so
///          callers only need to provide the Machine IR and target description.
/// @param func Machine function to allocate in place.
/// @param target Target lowering information describing available registers.
/// @return Summary of the allocation, including spill slot usage.
AllocationResult allocate(MFunction &func, const TargetInfo &target)
{
    ra::LiveIntervals intervals{};
    intervals.run(func);

    ra::LinearScanAllocator allocator{func, target, intervals};
    return allocator.run();
}

} // namespace viper::codegen::x64
