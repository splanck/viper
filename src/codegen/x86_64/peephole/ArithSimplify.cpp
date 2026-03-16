//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/ArithSimplify.cpp
// Purpose: Arithmetic simplification peephole sub-passes for the x86-64 backend.
//          Implements MOV-zero to XOR, CMP-zero to TEST, arithmetic identity
//          elimination, and multiply strength reduction.
//
// Key invariants:
//   - All flag-clobbering rewrites check nextInstrReadsFlags before applying.
//   - Strength reduction only applies when EFLAGS semantics are preserved.
//
// Ownership/Lifetime:
//   - Stateless; all state is owned by the caller.
//
// Links: src/codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "ArithSimplify.hpp"

namespace viper::codegen::x64::peephole
{

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

bool tryArithmeticIdentity(const std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats)
{
    const auto &instr = instrs[idx];
    switch (instr.opcode)
    {
        case MOpcode::ADDri:
            // add reg, #0 -> no-op (but sets flags: ZF=1 iff reg==0, CF=0, OF=0)
            if (instr.operands.size() == 2 && isZeroImm(instr.operands[1]))
            {
                if (nextInstrReadsFlags(instrs, idx))
                    return false;
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        case MOpcode::ORri:
        case MOpcode::XORri:
            // or reg, #0 -> no-op | xor reg, #0 -> no-op (but sets flags)
            if (instr.operands.size() == 2 && isZeroImm(instr.operands[1]))
            {
                if (nextInstrReadsFlags(instrs, idx))
                    return false;
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        case MOpcode::ANDri:
            // and reg, #-1 -> no-op (AND with all-ones is identity, but sets flags)
            if (instr.operands.size() == 2)
            {
                const auto *imm = std::get_if<OpImm>(&instr.operands[1]);
                if (imm && imm->val == -1)
                {
                    if (nextInstrReadsFlags(instrs, idx))
                        return false;
                    ++stats.arithmeticIdentities;
                    return true;
                }
            }
            break;

        case MOpcode::SHLri:
        case MOpcode::SHRri:
        case MOpcode::SARri:
            // shift by 0 -> no-op (but sets flags)
            if (instr.operands.size() == 2 && isZeroImm(instr.operands[1]))
            {
                if (nextInstrReadsFlags(instrs, idx))
                    return false;
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        default:
            break;
    }
    return false;
}

bool tryStrengthReduction(std::vector<MInstr> &instrs,
                          std::size_t idx,
                          const RegConstMap &knownConsts,
                          PeepholeStats &stats)
{
    auto &instr = instrs[idx];
    if (instr.opcode != MOpcode::IMULrr)
        return false;
    if (instr.operands.size() != 2)
        return false;

    // imul dst, src - check if src is a known power-of-2 constant
    auto srcConst = getConstValue(instr.operands[1], knownConsts);
    if (!srcConst)
        return false;

    int shiftAmount = log2IfPowerOf2(*srcConst);
    if (shiftAmount < 0 || shiftAmount > 63)
        return false;

    // Check if any following instruction reads flags set by IMUL.
    // SHL sets CF/OF differently than IMUL, so the transformation is unsafe
    // when flags are consumed.
    if (nextInstrReadsFlags(instrs, idx))
        return false;

    // Rewrite: imul dst, src -> shl dst, #shift
    instr.opcode = MOpcode::SHLri;
    instr.operands[1] = OpImm{shiftAmount};
    ++stats.strengthReductions;
    return true;
}

} // namespace viper::codegen::x64::peephole
