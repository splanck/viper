//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/PreRegAllocOpt.cpp
// Purpose: Conservative AArch64 MIR cleanup before register allocation:
//          identity-copy removal, single-use copy forwarding. The traversal
//          and safety conditions live in the shared PreRAForwardCopy
//          template; this file supplies the AArch64 MIR queries (copy
//          shapes, def positions, boundary opcodes).
//
// Key invariants:
//   - Operates on virtual registers only; no physical register assignment.
//   - Only eliminates copies that are provably safe (single use, no call crosses,
//     no aliasing in the def/use chain).
//
// Ownership/Lifetime:
//   - Borrows MFunction for the duration of the call; no persistent state.
//
// Links: codegen/common/PreRAForwardCopy.hpp,
//        codegen/aarch64/PreRegAllocOpt.hpp,
//        codegen/aarch64/passes/PreRegAllocOptPass.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/PreRegAllocOpt.hpp"

#include "codegen/common/PreRAForwardCopy.hpp"

namespace viper::codegen::aarch64 {
namespace {

/// @brief Return true if @p lhs and @p rhs refer to the same physical or virtual register.
[[nodiscard]] bool sameReg(const MReg &lhs, const MReg &rhs) noexcept {
    return lhs.isPhys == rhs.isPhys && lhs.cls == rhs.cls && lhs.idOrPhys == rhs.idOrPhys;
}

/// @brief Return true if @p opcode is a register-to-register copy (MovRR or FMovRR).
[[nodiscard]] bool isCopyOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MovRR || opcode == MOpcode::FMovRR;
}

/// @brief Return true if @p opcode ends sequential flow within a block (calls excluded).
[[nodiscard]] bool isNonCallBoundaryOpcode(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Cbz:
        case MOpcode::Cbnz:
        case MOpcode::Ret:
            return true;
        default:
            return false;
    }
}

/// @brief Return true if @p opcode writes its result into the first operand slot (ops[0]).
[[nodiscard]] bool definesFirstOperand(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::FMovGR:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
        case MOpcode::LdrRegFpImm:
        case MOpcode::Ldr8RegFpImm:
        case MOpcode::Ldr16RegFpImm:
        case MOpcode::Ldr32RegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::Ldr8RegBaseImm:
        case MOpcode::Ldr16RegBaseImm:
        case MOpcode::Ldr32RegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::AddFpImm:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::UmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::MSubRRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::LslvRRR:
        case MOpcode::LsrvRRR:
        case MOpcode::AsrvRRR:
        case MOpcode::Cset:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::MAddRRRR:
        case MOpcode::Csel:
        case MOpcode::AddsRRR:
        case MOpcode::SubsRRR:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI:
        case MOpcode::AddOvfRRR:
        case MOpcode::SubOvfRRR:
        case MOpcode::AddOvfRI:
        case MOpcode::SubOvfRI:
        case MOpcode::MulOvfRRR:
            return true;
        default:
            return false;
    }
}

/// @brief Return true if @p operand is a register operand naming the same register as @p reg.
[[nodiscard]] bool operandIsReg(const MOperand &operand, const MReg &reg) noexcept {
    return operand.kind == MOperand::Kind::Reg && sameReg(operand.reg, reg);
}

/// @brief Return true if operand at @p operandIndex in @p instr is a register use (not a def).
[[nodiscard]] bool operandIsUse(const MInstr &instr, std::size_t operandIndex) noexcept {
    if (instr.ops[operandIndex].kind != MOperand::Kind::Reg)
        return false;
    if (definesFirstOperand(instr.opc) && operandIndex == 0)
        return false;
    if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
        operandIndex < 2) {
        return false;
    }
    return true;
}

/// @brief AArch64 traits for the shared pre-RA copy forwarding template.
struct A64PreRATraits {
    using BlockT = MBasicBlock;
    using InstrT = MInstr;
    using RegT = MReg;

    static std::vector<MInstr> &instrs(MBasicBlock &block) {
        return block.instrs;
    }

    static const std::vector<MInstr> &instrs(const MBasicBlock &block) {
        return block.instrs;
    }

    static bool isIdentityCopy(const MInstr &instr) {
        if (!isCopyOpcode(instr.opc) || instr.ops.size() != 2)
            return false;
        if (instr.ops[0].kind != MOperand::Kind::Reg || instr.ops[1].kind != MOperand::Kind::Reg)
            return false;
        return sameReg(instr.ops[0].reg, instr.ops[1].reg);
    }

    static bool isForwardableCopy(const MInstr &instr, MReg &dst, MReg &src) {
        if (!isCopyOpcode(instr.opc) || instr.ops.size() != 2)
            return false;
        if (instr.ops[0].kind != MOperand::Kind::Reg || instr.ops[1].kind != MOperand::Kind::Reg)
            return false;
        const MReg &dstReg = instr.ops[0].reg;
        const MReg &srcReg = instr.ops[1].reg;
        // Physical sources such as ABI return registers are not tracked as
        // live ranges here. Forwarding them would let register allocation
        // reuse that physical register before the forwarded use.
        if (dstReg.isPhys || srcReg.isPhys || dstReg.cls != srcReg.cls || sameReg(dstReg, srcReg))
            return false;
        dst = dstReg;
        src = srcReg;
        return true;
    }

    static bool definesReg(const MInstr &instr, const MReg &reg) {
        if (definesFirstOperand(instr.opc) && !instr.ops.empty() && operandIsReg(instr.ops[0], reg))
            return true;
        if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
            instr.ops.size() >= 2) {
            return operandIsReg(instr.ops[0], reg) || operandIsReg(instr.ops[1], reg);
        }
        return false;
    }

    static bool isCall(const MInstr &instr) {
        return instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr;
    }

    static bool isNonCallBoundary(const MInstr &instr) {
        return isNonCallBoundaryOpcode(instr.opc);
    }

    static common::PreRAUseScan scanUses(const MInstr &instr, const MReg &dst) {
        common::PreRAUseScan scan{};
        for (std::size_t opIdx = 0; opIdx < instr.ops.size(); ++opIdx) {
            if (!operandIsUse(instr, opIdx))
                continue;
            if (!operandIsReg(instr.ops[opIdx], dst))
                continue;
            ++scan.useCount;
            ++scan.directUseCount;
            scan.directOperand = opIdx;
        }
        return scan;
    }

    static void forwardUse(MInstr &use, std::size_t operandIndex, const MInstr &copy) {
        use.ops[operandIndex] = copy.ops[1];
    }
};

} // namespace

std::size_t runPreRegAllocOpt(MFunction &fn) {
    return common::runPreRAForwardCopy<A64PreRATraits>(fn);
}

} // namespace viper::codegen::aarch64
