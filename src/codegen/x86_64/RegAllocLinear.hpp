//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/RegAllocLinear.hpp
// Purpose: Declare the Phase A linear-scan register allocator for x86-64.
// Key invariants:
//   - Live intervals are computed before allocation.
//   - Spill slots are assigned monotonically per register class.
//   - The vregToPhys map covers all virtual registers after allocation completes.
// Ownership/Lifetime:
//   - Functions operate directly on the supplied MFunction; return results by value.
//   - No persistent allocator state between calls.
// Links: codegen/x86_64/RegAllocLinear.cpp,
//        codegen/x86_64/MachineIR.hpp,
//        codegen/x86_64/TargetX64.hpp,
//        codegen/x86_64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"

#include <unordered_map>

namespace zanna::codegen::x64 {

/// \brief Result of a linear-scan allocation.
struct AllocationResult {
    std::unordered_map<uint16_t, PhysReg> vregToPhys; ///< Final vreg → phys mapping.
    int spillSlotsGPR{0};                             ///< Number of 8-byte GPR spill slots.
    int spillSlotsXMM{0};                             ///< Number of 8-byte XMM spill slots.
};

/// \brief Allocate registers for \p func using a basic linear-scan strategy.
///
/// @details Computes live intervals for every virtual register, then walks
///          them in start-point order assigning physical registers from the
///          available pool. When no register is free the interval with the
///          longest remaining range is spilled. After allocation, all virtual
///          register references in @p func are rewritten to their assigned
///          physical registers.
///
/// @param func   The machine function whose virtual registers are allocated.
///               Modified in-place with spill/reload instructions as needed.
/// @param target ABI metadata providing allocatable register sets and
///               callee-saved information.
/// @return An AllocationResult containing the vreg-to-phys mapping and the
///         number of spill slots consumed per register class.
[[nodiscard]] AllocationResult allocate(MFunction &func, const TargetInfo &target);

} // namespace zanna::codegen::x64
