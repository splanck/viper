// src/codegen/x86_64/FrameLowering.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Declare utilities responsible for constructing stack frames for the
//          x86-64 backend during Phase A bring-up. The routines emit function
//          prologues/epilogues and resolve spill slot displacements after
//          register allocation has annotated Machine IR with abstract stack
//          references.
// Invariants: FrameInfo summarises the size requirements for the various frame
//             regions. Functions operate directly on Machine IR blocks and
//             mutate them in-place while preserving deterministic instruction
//             ordering.
// Ownership: Callers retain ownership of Machine IR objects. Utilities borrow
//            references and never allocate dynamic resources beyond standard
//            containers.
// Notes: Header depends on MachineIR.hpp and TargetX64.hpp only.

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
