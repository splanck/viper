//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/RegClassify.hpp
// Purpose: Register classification predicates for the AArch64 register
//          allocator (allocatable GPR test, argument register test).
// Key invariants:
//   - isAllocatableGPR excludes X29, X30, SP, X18, X9, and X16 (scratch).
//   - isArgRegister checks both integer and FP argument orders.
// Ownership/Lifetime:
//   - Stateless free functions; no ownership.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64::ra {

/// @brief Check if a GPR is available for register allocation.
/// @param r The physical register to check.
/// @return True if the register can be allocated to virtual registers.
/// @details Excludes frame pointer (X29), link register (X30), stack pointer (SP),
///          platform reserved (X18), and the global scratch registers (X9/X16).
inline bool isAllocatableGPR(PhysReg r) {
    switch (r) {
        case PhysReg::X29:
        case PhysReg::X30:
        case PhysReg::SP:
        case PhysReg::X18:
        // Reserve the global scratch registers so the allocator never hands them out.
        case PhysReg::X9:
        case PhysReg::X16:
            return false;
        default:
            return isGPR(r);
    }
}

/// @brief Check if a register is used for argument passing.
/// @param r The physical register to check.
/// @param ti Target information containing the ABI's argument register lists.
/// @return True if the register is in the integer or floating-point argument order.
inline bool isArgRegister(PhysReg r, const TargetInfo &ti) {
    for (auto ar : ti.intArgOrder)
        if (ar == r)
            return true;
    for (auto ar : ti.f64ArgOrder)
        if (ar == r)
            return true;
    return false;
}

} // namespace viper::codegen::aarch64::ra
