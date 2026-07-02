//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/ra/OperandRoles.cpp
// Purpose: Implementation of operand role classification for every MOpcode
//          that carries register operands.
//
// Key invariants:
//   - Must stay in sync with MachineIR.hpp opcode additions.
//
// Ownership/Lifetime:
//   - Stateless free function; no ownership.
//
// Links: codegen/aarch64/ra/OperandRoles.hpp,
//        codegen/aarch64/ra/OpcodeClassify.hpp
//
//===----------------------------------------------------------------------===//

#include "OperandRoles.hpp"

#include "OpcodeClassify.hpp"

#include <stdexcept>
#include <string>

namespace viper::codegen::aarch64::ra {

std::pair<bool, bool> operandRoles(const MInstr &ins, std::size_t idx) {
    // Returns {isUse, isDef}
    switch (ins.opc) {
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
        case MOpcode::FRintN:
            return {idx == 1, idx == 0};
        default:
            break;
    }

    // Shifted-operand ALU: dst = a op (b << k); ops[3] is the shift amount.
    if (ins.opc == MOpcode::AddRRRLsl || ins.opc == MOpcode::SubRRRLsl ||
        ins.opc == MOpcode::AndRRRLsl || ins.opc == MOpcode::OrrRRRLsl ||
        ins.opc == MOpcode::EorRRRLsl) {
        if (idx == 0)
            return {false, true};
        if (idx == 1 || idx == 2)
            return {true, false};
        return {false, false};
    }

    // AArch64 3-address ALU: dst = lhs op rhs
    // Operand 0 is def-only (not read by the instruction).
    if (isThreeAddrRRR(ins.opc)) {
        if (idx == 0)
            return {false, true};
        if (idx == 1 || idx == 2)
            return {true, false};
    }

    // FP RRR behave like integer RRR (3-address: dst = lhs op rhs)
    if (ins.opc == MOpcode::FAddRRR || ins.opc == MOpcode::FSubRRR || ins.opc == MOpcode::FMulRRR ||
        ins.opc == MOpcode::FDivRRR) {
        if (idx == 0)
            return {false, true};
        if (idx == 1 || idx == 2)
            return {true, false};
    }

    if (isUseDefImmLike(ins.opc)) {
        if (idx == 0)
            return {false, true};
        if (idx == 1)
            return {true, false};
    }

    if (ins.opc == MOpcode::SCvtF || ins.opc == MOpcode::FCvtZS || ins.opc == MOpcode::UCvtF ||
        ins.opc == MOpcode::FCvtZU) {
        return {idx == 1, idx == 0};
    }

    if (isCmpRR(ins.opc) || ins.opc == MOpcode::TstRR || ins.opc == MOpcode::FCmpRR)
        return {true, false};

    if (isCmpRI(ins.opc) && idx == 0)
        return {true, false};

    if (isCset(ins.opc))
        return {false, idx == 0};

    if (isMemLd(ins.opc)) {
        if (idx == 0)
            return {false, true};
        if ((ins.opc == MOpcode::LdrRegBaseImm || ins.opc == MOpcode::Ldr8RegBaseImm ||
             ins.opc == MOpcode::Ldr16RegBaseImm || ins.opc == MOpcode::Ldr32RegBaseImm ||
             ins.opc == MOpcode::LdrFprBaseImm) &&
            idx == 1)
            return {true, false};
        if ((ins.opc == MOpcode::LdrRegBaseRegLsl || ins.opc == MOpcode::Ldr32RegBaseRegLsl ||
             ins.opc == MOpcode::LdrFprBaseRegLsl) &&
            (idx == 1 || idx == 2))
            return {true, false};
        return {false, false};
    }

    if (isMemSt(ins.opc)) {
        if (idx == 0)
            return {true, false};
        if ((ins.opc == MOpcode::StrRegBaseImm || ins.opc == MOpcode::Str8RegBaseImm ||
             ins.opc == MOpcode::Str16RegBaseImm || ins.opc == MOpcode::Str32RegBaseImm ||
             ins.opc == MOpcode::StrFprBaseImm) &&
            idx == 1)
            return {true, false};
        if ((ins.opc == MOpcode::StrRegBaseRegLsl || ins.opc == MOpcode::Str32RegBaseRegLsl ||
             ins.opc == MOpcode::StrFprBaseRegLsl) &&
            (idx == 1 || idx == 2))
            return {true, false};
        return {false, false};
    }

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
    if (ins.opc == MOpcode::AddPageOff) {
        if (idx == 0)
            return {false, true}; // dst is def-only
        if (idx == 1)
            return {true, false}; // src is use
        return {false, false};    // label is neither
    }

    // MSubRRRR / MAddRRRR: msub/madd dst, mul1, mul2, acc
    // Operands: [0]=dst (def), [1]=mul1 (use), [2]=mul2 (use), [3]=acc (use)
    if (ins.opc == MOpcode::MSubRRRR || ins.opc == MOpcode::MAddRRRR) {
        if (idx == 0)
            return {false, true}; // dst is def-only
        return {true, false};     // all others are use-only
    }

    // Cbz / Cbnz: cbz/cbnz reg, label => branch if reg is zero/nonzero
    // Operands: [0]=reg (use), [1]=label
    if (ins.opc == MOpcode::Cbz || ins.opc == MOpcode::Cbnz) {
        if (idx == 0)
            return {true, false}; // reg is use
        return {false, false};    // label is neither
    }

    // Blr reads a register call target. Direct Bl carries only a label operand.
    if (ins.opc == MOpcode::Blr)
        return {idx == 0, false};

    // Csel: csel dst, trueReg, falseReg, cond
    // Operands: [0]=dst (def), [1]=trueReg (use), [2]=falseReg (use), [3]=cond
    if (ins.opc == MOpcode::Csel) {
        if (idx == 0)
            return {false, true}; // dst is def-only
        if (idx == 1 || idx == 2)
            return {true, false}; // sources are use-only
        return {false, false};    // cond is neither
    }

    // LdpRegFpImm / LdpFprFpImm: ldp r1, r2, [fp, #offset]
    // Operands: [0]=r1 (def), [1]=r2 (def), [2]=offset (imm)
    if (ins.opc == MOpcode::LdpRegFpImm || ins.opc == MOpcode::LdpFprFpImm) {
        if (idx == 0 || idx == 1)
            return {false, true}; // both dests are def-only
        return {false, false};    // offset
    }

    // StpRegFpImm / StpFprFpImm: stp r1, r2, [fp, #offset]
    // Operands: [0]=r1 (use), [1]=r2 (use), [2]=offset (imm)
    if (ins.opc == MOpcode::StpRegFpImm || ins.opc == MOpcode::StpFprFpImm) {
        if (idx == 0 || idx == 1)
            return {true, false}; // both sources are use-only
        return {false, false};    // offset
    }

    if (idx < ins.ops.size() && ins.ops[idx].kind != MOperand::Kind::Reg)
        return {false, false};

    throw std::logic_error("AArch64 register allocator: unclassified MIR register operand role for " +
                           std::string(opcodeName(ins.opc)));
}

} // namespace viper::codegen::aarch64::ra
