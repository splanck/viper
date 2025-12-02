//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/RegAllocLinear.hpp
// Purpose: Linear-scan register allocator for AArch64 Machine IR.
//
// This allocator uses a simple linear-scan approach to map virtual registers
// to physical registers. When register pressure exceeds available registers,
// it spills values to the stack and reloads them as needed.
//
// Key invariants:
// - After allocation, all MReg operands have isPhys=true.
// - Spill slots are allocated in the function's frame layout.
// - Callee-saved registers are tracked for prologue/epilogue generation.
//
// Ownership/Lifetime:
// - Modifies the MFunction in place; caller owns the MFunction.
// - Uses TargetInfo for available registers and calling convention.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

/// @brief Results from register allocation.
struct AllocationResult
{
    int gprSpillSlots{0}; ///< Number of GPR spill slots allocated.
};

/// @brief Perform linear-scan register allocation on a machine function.
///
/// Rewrites virtual register operands to physical registers, inserting
/// spill/reload code as necessary. Updates the function's frame layout
/// with spill slot information.
///
/// @param fn The machine function to allocate registers for (modified in place).
/// @param ti Target information providing available registers.
/// @return Allocation statistics.
[[nodiscard]] AllocationResult allocate(MFunction &fn, const TargetInfo &ti);

} // namespace viper::codegen::aarch64
