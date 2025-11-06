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

/// @file
/// @brief Entry point that wires together the linear-scan register allocator.
/// @details Provides the glue that runs live-interval analysis and feeds the
///          resulting data into @ref ra::LinearScanAllocator.  Housing the logic
///          here keeps the public header lightweight while documenting the exact
///          sequencing of allocation phases used by the backend.

namespace viper::codegen::x64
{

/// @brief Run the linear-scan register allocator over a machine function.
/// @details The helper performs three steps:
///          1. Construct @ref ra::LiveIntervals and walk @p func to compute
///             closed-open [start, end) ranges for each virtual register.
///          2. Materialise @ref ra::LinearScanAllocator with the function, target
///             description, and interval data.
///          3. Invoke the allocator, returning its @ref AllocationResult so later
///             passes know which temporaries were spilled.
///          Owning the orchestration here keeps call sites concise and documents
///          the canonical ordering of analysis and allocation phases.
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
