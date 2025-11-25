//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/FrameLowering.hpp
// Purpose: Declare utilities responsible for constructing stack frames for the
// Key invariants: To be documented.
// Ownership/Lifetime: Callers retain ownership of Machine IR objects. Utilities borrow
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"

#include <vector>

namespace viper::codegen::x64
{

/// \brief Summarises stack frame requirements for a machine function.
/// \details Areas are expressed in bytes. Spill slots are accounted for as
///          8-byte entries because the Phase A backend presently stores
///          scalars and double-precision values only.
struct FrameInfo
{
    int spillAreaGPR{0};                    ///< Total bytes reserved for GPR spills.
    int spillAreaXMM{0};                    ///< Total bytes reserved for XMM spills.
    int outgoingArgArea{0};                 ///< Bytes reserved for stack-based call arguments.
    int frameSize{0};                       ///< Total size of the frame below %rbp.
    std::vector<PhysReg> usedCalleeSaved{}; ///< Callee-saved registers touched by the function.
};

/// \brief Inserts prologue and epilogue instructions following SysV ABI rules.
void insertPrologueEpilogue(MFunction &func, const TargetInfo &target, const FrameInfo &frame);

/// \brief Assigns concrete stack displacements to spill slots and records frame usage.
void assignSpillSlots(MFunction &func, const TargetInfo &target, FrameInfo &frame);

} // namespace viper::codegen::x64
