// src/codegen/x86_64/Peephole.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Implement conservative Machine IR peephole optimisations that fold
//          redundant zeroing moves and zero-comparisons into cheaper
//          instruction forms for the x86-64 backend.
// Invariants: Only exact MOVri and CMPri patterns with register destinations
//             and zero immediates are rewritten, leaving all other instruction
//             encodings untouched.
// Ownership: Mutates Machine IR instructions in place without taking ownership
//            of the underlying function or basic blocks.
// Notes: Depends solely on Peephole.hpp and the Machine IR definitions it pulls
//        in.

#include "Peephole.hpp"

#include <cstddef>
#include <variant>

namespace viper::codegen::x64
{
namespace
{
[[nodiscard]] const OpReg *asReg(const Operand &operand) noexcept
{
    return std::get_if<OpReg>(&operand);
}

[[nodiscard]] OpReg *asReg(Operand &operand) noexcept
{
    return std::get_if<OpReg>(&operand);
}

[[nodiscard]] const OpImm *asImm(const Operand &operand) noexcept
{
    return std::get_if<OpImm>(&operand);
}

[[nodiscard]] bool isZeroImmediate(const Operand &operand) noexcept
{
    const auto *imm = asImm(operand);
    return imm != nullptr && imm->val == 0;
}

[[nodiscard]] bool isGprRegister(const Operand &operand) noexcept
{
    const auto *reg = asReg(operand);
    return reg != nullptr && reg->cls == RegClass::GPR;
}

void foldMovZero(MInstr &instr) noexcept
{
    if (instr.operands.size() != 2)
    {
        return;
    }
    if (!isGprRegister(instr.operands[0]))
    {
        return;
    }
    if (!isZeroImmediate(instr.operands[1]))
    {
        return;
    }

    instr.opcode = MOpcode::XORrr32;
    instr.operands[1] = instr.operands[0];
}

void foldCmpZero(MInstr &instr) noexcept
{
    if (instr.operands.size() != 2)
    {
        return;
    }
    if (!isGprRegister(instr.operands[0]))
    {
        return;
    }
    if (!isZeroImmediate(instr.operands[1]))
    {
        return;
    }

    instr.opcode = MOpcode::TESTrr;
    instr.operands[1] = instr.operands[0];
}
} // namespace

void runPeepholes(MFunction &fn) noexcept
{
    for (auto &block : fn.blocks)
    {
        for (auto &instr : block.instructions)
        {
            switch (instr.opcode)
            {
                case MOpcode::MOVri:
                    foldMovZero(instr);
                    break;
                case MOpcode::CMPri:
                    foldCmpZero(instr);
                    break;
                default:
                    break;
            }
        }
    }
}
} // namespace viper::codegen::x64
