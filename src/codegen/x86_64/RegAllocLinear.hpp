//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/RegAllocLinear.hpp
// Purpose: Declare the Phase A linear-scan register allocator for the x86-64
// Key invariants: To be documented.
// Ownership/Lifetime: Functions operate directly on the supplied MFunction instance and
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"

#include <unordered_map>

namespace viper::codegen::x64
{

/// \brief Result of a linear-scan allocation.
struct AllocationResult
{
    std::unordered_map<uint16_t, PhysReg> vregToPhys; ///< Final vreg → phys mapping.
    int spillSlotsGPR{0};                             ///< Number of 8-byte GPR spill slots.
    int spillSlotsXMM{0};                             ///< Number of 8-byte XMM spill slots.
};

/// \brief Allocate registers for \p func using a basic linear-scan strategy.
[[nodiscard]] AllocationResult allocate(MFunction &func, const TargetInfo &target);

} // namespace viper::codegen::x64
