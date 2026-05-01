//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/PreRegAllocOpt.cpp
// Purpose: Implement conservative AArch64 MIR cleanup before register allocation.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/PreRegAllocOpt.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace viper::codegen::aarch64 {
namespace {

struct UseSite {
    std::size_t instrIndex{0};
    std::size_t operandIndex{0};
};

[[nodiscard]] bool sameReg(const MReg &lhs, const MReg &rhs) noexcept {
    return lhs.isPhys == rhs.isPhys && lhs.cls == rhs.cls && lhs.idOrPhys == rhs.idOrPhys;
}

[[nodiscard]] bool isCopyOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MovRR || opcode == MOpcode::FMovRR;
}

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
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
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

[[nodiscard]] bool operandIsReg(const MOperand &operand, const MReg &reg) noexcept {
    return operand.kind == MOperand::Kind::Reg && sameReg(operand.reg, reg);
}

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

[[nodiscard]] bool definesReg(const MInstr &instr, const MReg &reg) noexcept {
    if (definesFirstOperand(instr.opc) && !instr.ops.empty() && operandIsReg(instr.ops[0], reg))
        return true;
    if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
        instr.ops.size() >= 2) {
        return operandIsReg(instr.ops[0], reg) || operandIsReg(instr.ops[1], reg);
    }
    return false;
}

[[nodiscard]] bool isIdentityCopy(const MInstr &instr) noexcept {
    if (!isCopyOpcode(instr.opc) || instr.ops.size() != 2)
        return false;
    if (instr.ops[0].kind != MOperand::Kind::Reg || instr.ops[1].kind != MOperand::Kind::Reg)
        return false;
    return sameReg(instr.ops[0].reg, instr.ops[1].reg);
}

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
