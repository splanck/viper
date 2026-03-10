//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/OperandRoles.cpp
// Purpose: Implementation of operand role classification for every MOpcode
//          that carries register operands.
// Key invariants:
//   - Must stay in sync with MachineIR.hpp opcode additions.
// Ownership/Lifetime:
//   - See OperandRoles.hpp.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "OperandRoles.hpp"

#include "OpcodeClassify.hpp"

namespace viper::codegen::aarch64::ra
{

std::pair<bool, bool> operandRoles(const MInstr &ins, std::size_t idx)
{
    // Returns {isUse, isDef}
    switch (ins.opc)
    {
        case MOpcode::MovRR:
            return {idx == 1, idx == 0};
        case MOpcode::MovRI:
            return {false, idx == 0};
        case MOpcode::FMovRR:
            return {idx == 1, idx == 0};
        case MOpcode::FMovRI:
            return {false, idx == 0};
        case MOpcode::FMovGR:
            return {idx == 1, idx == 0};
        default:
            break;
    }

    // AArch64 3-address ALU: dst = lhs op rhs
    // Operand 0 is def-only (not read by the instruction).
    if (isThreeAddrRRR(ins.opc))
    {
        if (idx == 0)
            return {false, true};
        if (idx == 1 || idx == 2)
            return {true, false};
    }

    // FP RRR behave like integer RRR (3-address: dst = lhs op rhs)
    if (ins.opc == MOpcode::FAddRRR || ins.opc == MOpcode::FSubRRR ||
        ins.opc == MOpcode::FMulRRR || ins.opc == MOpcode::FDivRRR)
    {
        if (idx == 0)
            return {false, true};
        if (idx == 1 || idx == 2)
            return {true, false};
    }

    if (isUseDefImmLike(ins.opc))
    {
        if (idx == 0)
            return {true, true};
        if (idx == 1)
            return {true, false};
    }

    if (ins.opc == MOpcode::SCvtF || ins.opc == MOpcode::FCvtZS || ins.opc == MOpcode::UCvtF ||
        ins.opc == MOpcode::FCvtZU)
    {
        return {idx == 1, idx == 0};
    }

    if (isCmpRR(ins.opc) || ins.opc == MOpcode::FCmpRR)
        return {true, false};

    if (isCmpRI(ins.opc) && idx == 0)
        return {true, false};

    if (isCset(ins.opc))
        return {false, idx == 0};

    if (isMemLd(ins.opc) && idx == 0)
        return {false, true};

    if (isMemSt(ins.opc) && idx == 0)
        return {true, false};

    if (ins.opc == MOpcode::LdrFprFpImm)
        return {false, idx == 0};

    if ((ins.opc == MOpcode::StrFprFpImm || ins.opc == MOpcode::StrFprSpImm) && idx == 0)
        return {true, false};

    // Phi-edge copies: operand 0 is the source vreg (USE only).
    // After RA they become StrRegFpImm / StrFprFpImm.
    if ((ins.opc == MOpcode::PhiStoreGPR || ins.opc == MOpcode::PhiStoreFPR) && idx == 0)
        return {true, false};

    // AddFpImm: dst = fp + imm (alloca address materialisation).
    // Operand 0 is def-only (the computed address); operand 1 is an immediate.
    if (ins.opc == MOpcode::AddFpImm)
        return {false, idx == 0};

    // AdrPage: dst is def-only, label operand is not a register
    if (ins.opc == MOpcode::AdrPage)
        return {false, idx == 0};

    // AddPageOff: dst is def+use (same reg for src and dst), label operand is not a register
    if (ins.opc == MOpcode::AddPageOff)
    {
        if (idx == 0)
            return {false, true}; // dst is def-only
        if (idx == 1)
            return {true, false}; // src is use
        return {false, false};    // label is neither
    }

    // MSubRRRR / MAddRRRR: msub/madd dst, mul1, mul2, acc
    // Operands: [0]=dst (def), [1]=mul1 (use), [2]=mul2 (use), [3]=acc (use)
    if (ins.opc == MOpcode::MSubRRRR || ins.opc == MOpcode::MAddRRRR)
    {
        if (idx == 0)
            return {false, true}; // dst is def-only
        return {true, false};     // all others are use-only
    }

    // Cbz / Cbnz: cbz/cbnz reg, label => branch if reg is zero/nonzero
    // Operands: [0]=reg (use), [1]=label
    if (ins.opc == MOpcode::Cbz || ins.opc == MOpcode::Cbnz)
    {
        if (idx == 0)
            return {true, false}; // reg is use
        return {false, false};    // label is neither
    }

    // Csel: csel dst, trueReg, falseReg, cond
    // Operands: [0]=dst (def), [1]=trueReg (use), [2]=falseReg (use), [3]=cond
    if (ins.opc == MOpcode::Csel)
    {
        if (idx == 0)
            return {false, true}; // dst is def-only
        if (idx == 1 || idx == 2)
            return {true, false}; // sources are use-only
        return {false, false};    // cond is neither
    }

    // LdpRegFpImm / LdpFprFpImm: ldp r1, r2, [fp, #offset]
    // Operands: [0]=r1 (def), [1]=r2 (def), [2]=offset (imm)
    if (ins.opc == MOpcode::LdpRegFpImm || ins.opc == MOpcode::LdpFprFpImm)
    {
        if (idx == 0 || idx == 1)
            return {false, true}; // both dests are def-only
        return {false, false};    // offset
    }

    // StpRegFpImm / StpFprFpImm: stp r1, r2, [fp, #offset]
    // Operands: [0]=r1 (use), [1]=r2 (use), [2]=offset (imm)
    if (ins.opc == MOpcode::StpRegFpImm || ins.opc == MOpcode::StpFprFpImm)
    {
        if (idx == 0 || idx == 1)
            return {true, false}; // both sources are use-only
        return {false, false};    // offset
    }

    return {true, false};
}

} // namespace viper::codegen::aarch64::ra
