// src/codegen/aarch64/RegAllocLinear.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Declare a minimal linear-scan register allocator for the AArch64
// //        backend used during bring-up. The allocator supports virtual GPRs,
// //        assigns physical registers from the target pools, inserts spill
// //        code using FP-relative slots when pressure exceeds capacity, and
// //        records callee-saved usage via MFunction::savedGPRs for the
// //        prologue/epilogue.
// Invariants: Mutates the supplied Machine IR in-place. All vregs must be
//             resolved to physical registers before emission.

#pragma once

#include "MachineIR.hpp"
#include "TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

struct AllocationResult
{
    int gprSpillSlots{0};
};

[[nodiscard]] AllocationResult allocate(MFunction &fn, const TargetInfo &ti);

} // namespace viper::codegen::aarch64

