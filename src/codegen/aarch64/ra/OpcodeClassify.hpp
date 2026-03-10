//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/OpcodeClassify.hpp
// Purpose: MOpcode classification predicates used by the AArch64 register
//          allocator to determine operand roles and instruction categories.
// Key invariants:
//   - Each predicate is a pure function of MOpcode with no side effects.
//   - Classification must stay in sync with MachineIR.hpp opcode definitions.
// Ownership/Lifetime:
//   - Stateless free functions; no ownership.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

namespace viper::codegen::aarch64::ra
{

/// @brief Check if an opcode is a three-operand ALU operation (dst = src1 op src2).
/// @param opc The machine opcode to check.
/// @return True for three-operand ALU operations where operand 0 is def-only.
inline bool isThreeAddrRRR(MOpcode opc)
{
    switch (opc)
    {
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::LslvRRR:
        case MOpcode::LsrvRRR:
        case MOpcode::AsrvRRR:
        case MOpcode::AddsRRR:
        case MOpcode::SubsRRR:
        case MOpcode::AddOvfRRR:
        case MOpcode::SubOvfRRR:
        case MOpcode::MulOvfRRR:
            return true;
        default:
            return false;
    }
}

/// @brief Check if an opcode uses a use-def-immediate register pattern.
/// @param opc The machine opcode to check.
/// @return True for two-operand ALU operations with immediate (dst = src op imm).
inline bool isUseDefImmLike(MOpcode opc)
{
    switch (opc)
    {
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI:
        case MOpcode::AddOvfRI:
        case MOpcode::SubOvfRI:
            return true;
        default:
            return false;
    }
}

/// @brief Check if an opcode adjusts the stack pointer.
/// @param opc The machine opcode to check.
/// @return True for SubSpImm or AddSpImm opcodes.
inline bool isSpAdj(MOpcode opc)
{
    return opc == MOpcode::SubSpImm || opc == MOpcode::AddSpImm;
}

/// @brief Check if an opcode is an unconditional or conditional branch.
/// @param opc The machine opcode to check.
/// @return True for Br or BCond opcodes.
/// @note Cbz is not included since it has a register operand needing allocation.
inline bool isBranch(MOpcode opc)
{
    return opc == MOpcode::Br || opc == MOpcode::BCond;
}

/// @brief Check if an opcode is a function call.
/// @param opc The machine opcode to check.
/// @return True for Bl (branch-and-link) or Blr (branch-link-register) opcode.
inline bool isCall(MOpcode opc)
{
    return opc == MOpcode::Bl || opc == MOpcode::Blr;
}

/// @brief Check if an opcode is a register-register comparison.
/// @param opc The machine opcode to check.
/// @return True for CmpRR opcode.
inline bool isCmpRR(MOpcode opc)
{
    return opc == MOpcode::CmpRR;
}

/// @brief Check if an opcode is a register-immediate comparison.
/// @param opc The machine opcode to check.
/// @return True for CmpRI opcode.
inline bool isCmpRI(MOpcode opc)
{
    return opc == MOpcode::CmpRI;
}

/// @brief Check if an opcode is a conditional set.
/// @param opc The machine opcode to check.
/// @return True for Cset opcode.
inline bool isCset(MOpcode opc)
{
    return opc == MOpcode::Cset;
}

/// @brief Check if an opcode is a return instruction.
/// @param opc The machine opcode to check.
/// @return True for Ret opcode.
inline bool isRet(MOpcode opc)
{
    return opc == MOpcode::Ret;
}

/// @brief Check if an opcode is a basic block terminator.
/// @param opc The machine opcode to check.
/// @return True for branches and returns.
inline bool isTerminator(MOpcode opc)
{
    return isBranch(opc) || isRet(opc);
}

/// @brief Check if an opcode is a memory load instruction.
/// @param opc The machine opcode to check.
/// @return True for LdrRegFpImm or LdrRegBaseImm opcodes.
inline bool isMemLd(MOpcode opc)
{
    return opc == MOpcode::LdrRegFpImm || opc == MOpcode::LdrRegBaseImm;
}

/// @brief Check if an opcode is a memory store instruction.
/// @param opc The machine opcode to check.
/// @return True for StrRegFpImm, StrRegBaseImm, or StrRegSpImm opcodes.
inline bool isMemSt(MOpcode opc)
{
    return opc == MOpcode::StrRegFpImm || opc == MOpcode::StrRegBaseImm ||
           opc == MOpcode::StrRegSpImm;
}

} // namespace viper::codegen::aarch64::ra
