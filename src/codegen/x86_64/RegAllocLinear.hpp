// src/codegen/x86_64/RegAllocLinear.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare the Phase A linear-scan register allocator for the x86-64
//          backend. The allocator implements a deliberately simple strategy
//          tailored for bring-up: it performs block-local live range
//          approximation, selects registers directly from TargetInfo pools, and
//          falls back to stack slots when pressure requires spilling.
// Invariants: AllocationResult captures the mapping from virtual to physical
//             registers and the total number of stack slots reserved per class.
// Ownership: Functions operate directly on the supplied MFunction instance and
//            mutate its instruction streams in-place. No heap ownership beyond
//            standard containers.
// Notes: The allocator purposefully omits sophisticated liveness analysis; it
//        relies on blocks being visited in reverse-post-order and treats values
//        as live until block termination, which suffices for early backend
//        validation.

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"

#include <unordered_map>

namespace viper::codegen::x64 {

/// \brief Result of a linear-scan allocation.
struct AllocationResult {
  std::unordered_map<uint16_t, PhysReg> vregToPhys; ///< Final vreg → phys mapping.
  int spillSlotsGPR{0};                             ///< Number of 8-byte GPR spill slots.
  int spillSlotsXMM{0};                             ///< Number of 8-byte XMM spill slots.
};

/// \brief Allocate registers for \p func using a basic linear-scan strategy.
[[nodiscard]] AllocationResult allocate(MFunction& func, const TargetInfo& target);

} // namespace viper::codegen::x64

