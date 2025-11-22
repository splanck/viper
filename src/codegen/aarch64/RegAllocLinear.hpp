//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/RegAllocLinear.hpp
// Purpose: Declare a minimal linear-scan register allocator for the AArch64 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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

