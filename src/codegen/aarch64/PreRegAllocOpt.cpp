//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/PreRegAllocOpt.cpp
// Purpose: Conservative AArch64 MIR cleanup before register allocation:
//          identity-copy removal, single-use copy forwarding.
//
// Key invariants:
//   - Operates on virtual registers only; no physical register assignment.
//   - Only eliminates copies that are provably safe (single use, no call crosses,
//     no aliasing in the def/use chain).
//
// Ownership/Lifetime:
//   - Borrows MFunction for the duration of the call; no persistent state.
//
// Links: codegen/aarch64/PreRegAllocOpt.hpp,
//        codegen/aarch64/passes/PreRegAllocOptPass.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/PreRegAllocOpt.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace viper::codegen::aarch64 {
namespace {

/// @brief Location of a single register use within a basic block.
struct UseSite {
    std::size_t instrIndex{0};   ///< Index of the instruction that uses the register.
    std::size_t operandIndex{0}; ///< Operand slot index of the use within that instruction.
};

/// @brief Return true if @p lhs and @p rhs refer to the same physical or virtual register.
[[nodiscard]] bool sameReg(const MReg &lhs, const MReg &rhs) noexcept {
    return lhs.isPhys == rhs.isPhys && lhs.cls == rhs.cls && lhs.idOrPhys == rhs.idOrPhys;
}

/// @brief Return true if @p opcode is a register-to-register copy (MovRR or FMovRR).
[[nodiscard]] bool isCopyOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MovRR || opcode == MOpcode::FMovRR;
}

/// @brief Return true if @p opcode is a control-flow instruction that ends or suspends
///        sequential flow within a basic block (branches, calls, return).
[[nodiscard]] bool isBlockBoundary(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Cbz:
        case MOpcode::Cbnz:
        case MOpcode::Bl:
        case MOpcode::Blr:
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

/// @brief Return true if @p instr writes to @p reg (covers both scalar and LDP pair defs).
[[nodiscard]] bool definesReg(const MInstr &instr, const MReg &reg) noexcept {
    if (definesFirstOperand(instr.opc) && !instr.ops.empty() && operandIsReg(instr.ops[0], reg))
        return true;
    if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
        instr.ops.size() >= 2) {
        return operandIsReg(instr.ops[0], reg) || operandIsReg(instr.ops[1], reg);
    }
    return false;
}

/// @brief Return true if @p instr is a copy where source and destination are the same register.
[[nodiscard]] bool isIdentityCopy(const MInstr &instr) noexcept {
    if (!isCopyOpcode(instr.opc) || instr.ops.size() != 2)
        return false;
    if (instr.ops[0].kind != MOperand::Kind::Reg || instr.ops[1].kind != MOperand::Kind::Reg)
        return false;
    return sameReg(instr.ops[0].reg, instr.ops[1].reg);
}

/// @brief Find the single use of @p dst after the copy at @p copyIndex, if one exists.
/// @details Returns nullopt if dst is used more than once, used across a call boundary
///          where the source is also live, or if the use is itself a def.
/// @param block      The basic block containing the copy and its potential uses.
/// @param copyIndex  Index of the copy instruction whose destination we are tracking.
/// @param dst        The register defined by the copy (to track uses of).
/// @param src        The source register of the copy (to check for re-definition).
/// @return The single use site if exactly one safe forwarding candidate exists; nullopt otherwise.
[[nodiscard]] std::optional<UseSite> findSingleDirectUse(const MBasicBlock &block,
                                                         std::size_t copyIndex,
                                                         const MReg &dst,
                                                         const MReg &src) {
    std::optional<UseSite> site;

    bool crossedCall = false;
    for (std::size_t idx = copyIndex + 1; idx < block.instrs.size(); ++idx) {
        const auto &instr = block.instrs[idx];

        if (definesReg(instr, src) && !site)
            return std::nullopt;

        std::size_t useCount = 0;
        std::size_t directOperand = 0;
        for (std::size_t opIdx = 0; opIdx < instr.ops.size(); ++opIdx) {
            if (!operandIsUse(instr, opIdx))
                continue;
            if (!operandIsReg(instr.ops[opIdx], dst))
                continue;
            ++useCount;
            directOperand = opIdx;
        }

        if (useCount != 0) {
            if (crossedCall)
                return std::nullopt;
            if (useCount != 1 || definesReg(instr, dst))
                return std::nullopt;
            if (site)
                return std::nullopt;
            site = UseSite{idx, directOperand};
        }

        if (definesReg(instr, dst))
            break;
        if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr)
            crossedCall = true;
        if (isBlockBoundary(instr.opc) && instr.opc != MOpcode::Bl && instr.opc != MOpcode::Blr)
            break;
    }

    return site;
}

/// @brief Eliminate single-use virtual register copies by forwarding the source directly.
/// @details For each copy `dst = src`, if dst has exactly one use before it is
///          redefined, the use is replaced with src and the copy is erased.
/// @return Number of copy instructions removed.
std::size_t rewriteSingleUseCopies(MBasicBlock &block) {
    std::vector<bool> erase(block.instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t idx = 0; idx < block.instrs.size(); ++idx) {
        auto &instr = block.instrs[idx];
        if (!isCopyOpcode(instr.opc) || instr.ops.size() != 2)
            continue;
        if (instr.ops[0].kind != MOperand::Kind::Reg || instr.ops[1].kind != MOperand::Kind::Reg)
            continue;

        const MReg dst = instr.ops[0].reg;
        const MReg src = instr.ops[1].reg;
        // Physical sources such as ABI return registers are not tracked as
        // live ranges here. Forwarding them would let register allocation
        // reuse that physical register before the forwarded use.
        if (dst.isPhys || src.isPhys || dst.cls != src.cls || sameReg(dst, src))
            continue;

        auto site = findSingleDirectUse(block, idx, dst, src);
        if (!site)
            continue;

        block.instrs[site->instrIndex].ops[site->operandIndex] = instr.ops[1];
        erase[idx] = true;
        ++removed;
    }

    if (removed == 0)
        return 0;

    std::vector<MInstr> kept;
    kept.reserve(block.instrs.size() - removed);
    for (std::size_t idx = 0; idx < block.instrs.size(); ++idx) {
        if (!erase[idx])
            kept.push_back(std::move(block.instrs[idx]));
    }
    block.instrs = std::move(kept);
    return removed;
}

} // namespace

std::size_t runPreRegAllocOpt(MFunction &fn) {
    std::size_t removed = 0;
    for (auto &block : fn.blocks) {
        const auto oldSize = block.instrs.size();
        block.instrs.erase(std::remove_if(block.instrs.begin(), block.instrs.end(), isIdentityCopy),
                           block.instrs.end());
        removed += oldSize - block.instrs.size();
        removed += rewriteSingleUseCopies(block);
    }
    return removed;
}

} // namespace viper::codegen::aarch64
