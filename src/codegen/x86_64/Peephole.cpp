// src/codegen/x86_64/Peephole.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Implement conservative peephole optimisations over Machine IR for
//          the x86-64 backend.
// Invariants: Only applies rewrites to instruction forms that are semantically
//             equivalent under the current IR conventions.
// Ownership: Mutates Machine IR passed by reference without taking ownership of
//            any external resources.
// Notes: Peepholes remain intentionally minimal for Phase A experimentation.

#include "Peephole.hpp"

namespace viper::codegen::x64
{
namespace
{
[[nodiscard]] bool isZeroImm(const Operand &operand) noexcept
{
    const auto *imm = std::get_if<OpImm>(&operand);
    return imm != nullptr && imm->val == 0;
}

[[nodiscard]] bool isGprReg(const Operand &operand) noexcept
{
    const auto *reg = std::get_if<OpReg>(&operand);
    return reg != nullptr && reg->cls == RegClass::GPR;
}

void rewriteToXor(MInstr &instr, Operand regOperand)
{
    instr.opcode = MOpcode::XORrr32;
    instr.operands.clear();
    instr.operands.push_back(regOperand);
    instr.operands.push_back(regOperand);
}

void rewriteToTest(MInstr &instr, Operand regOperand)
{
    instr.opcode = MOpcode::TESTrr;
    instr.operands.clear();
    instr.operands.push_back(regOperand);
    instr.operands.push_back(regOperand);
}
} // namespace

void runPeepholes(MFunction &fn)
{
    for (auto &block : fn.blocks)
    {
        for (auto &instr : block.instructions)
        {
            switch (instr.opcode)
            {
                case MOpcode::MOVri:
                {
                    if (instr.operands.size() != 2)
                    {
                        break;
                    }

                    if (!isGprReg(instr.operands[0]) || !isZeroImm(instr.operands[1]))
                    {
                        break;
                    }

                    rewriteToXor(instr, instr.operands[0]);
                    break;
                }
                case MOpcode::CMPri:
                {
                    if (instr.operands.size() != 2)
                    {
                        break;
                    }

                    if (!isGprReg(instr.operands[0]) || !isZeroImm(instr.operands[1]))
                    {
                        break;
                    }

                    rewriteToTest(instr, instr.operands[0]);
                    break;
                }
                default:
                    break;
            }
        }
    }
}

} // namespace viper::codegen::x64
