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
//                     allocation summary consumed by later codegen passes.
// Links: src/codegen/x86_64/ra/LiveIntervals.hpp,
//        src/codegen/x86_64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//

#include "RegAllocLinear.hpp"

#include "ra/Allocator.hpp"
#include "ra/LiveIntervals.hpp"

namespace viper::codegen::x64
{

AllocationResult allocate(MFunction &func, const TargetInfo &target)
{
    ra::LiveIntervals intervals{};
    intervals.run(func);

    ra::LinearScanAllocator allocator{func, target, intervals};
    return allocator.run();
}

} // namespace viper::codegen::x64
