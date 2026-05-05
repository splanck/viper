//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/PreRegAllocOpt.cpp
// Purpose: Implement conservative x86-64 MIR cleanup before register allocation.
// Key invariants:
//   - Operates on virtual-register MIR only; physical sources are not forwarded.
//   - Single-use copy elimination does not cross block boundaries.
// Ownership/Lifetime:
//   - Stateless; mutates caller-owned MFunction in place.
// Links: codegen/x86_64/PreRegAllocOpt.hpp,
//        codegen/x86_64/OperandRoles.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/PreRegAllocOpt.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace viper::codegen::x64 {
namespace {

struct UseSite {
    std::size_t instrIndex{0};
    std::size_t operandIndex{0};
};

[[nodiscard]] bool sameReg(const OpReg &lhs, const OpReg &rhs) noexcept {
    return lhs.isPhys == rhs.isPhys && lhs.cls == rhs.cls && lhs.idOrPhys == rhs.idOrPhys;
}

[[nodiscard]] bool isCopyOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MOVrr || opcode == MOpcode::MOVSDrr;
}

[[nodiscard]] bool isBlockBoundary(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::CALL:
        case MOpcode::RET:
        case MOpcode::JMP:
        case MOpcode::JCC:
        case MOpcode::LABEL:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] const OpReg *asReg(const Operand &operand) noexcept {
    return std::get_if<OpReg>(&operand);
}

[[nodiscard]] OpReg *asReg(Operand &operand) noexcept {
    return std::get_if<OpReg>(&operand);
}

[[nodiscard]] bool memUsesReg(const OpMem &mem, const OpReg &reg) noexcept {
    if (sameReg(mem.base, reg))
        return true;
    return mem.hasIndex && sameReg(mem.index, reg);
}

[[nodiscard]] bool operandUsesReg(const Operand &operand, const OpReg &reg) noexcept {
    if (const auto *opReg = asReg(operand))
        return sameReg(*opReg, reg);
    if (const auto *mem = std::get_if<OpMem>(&operand))
        return memUsesReg(*mem, reg);
    return false;
}

[[nodiscard]] bool definesReg(const MInstr &instr, const OpReg &reg) noexcept {
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isUse;
        if (!isDef)
            continue;
        const auto *opReg = asReg(instr.operands[idx]);
        if (opReg && sameReg(*opReg, reg))
            return true;
    }
    return false;
}

[[nodiscard]] bool isIdentityCopy(const MInstr &instr) noexcept {
    if (!isCopyOpcode(instr.opcode) || instr.operands.size() != 2)
        return false;
    const auto *dst = asReg(instr.operands[0]);
    const auto *src = asReg(instr.operands[1]);
    return dst && src && sameReg(*dst, *src);
}

[[nodiscard]] std::optional<UseSite> findSingleDirectUse(const MBasicBlock &block,
                                                         std::size_t copyIndex,
                                                         const OpReg &dst,
                                                         const OpReg &src) {
    std::optional<UseSite> site;

    bool crossedCall = false;
    for (std::size_t idx = copyIndex + 1; idx < block.instructions.size(); ++idx) {
        const auto &instr = block.instructions[idx];

        if (definesReg(instr, src) && !site)
            return std::nullopt;

        std::size_t useCount = 0;
        std::size_t directUseCount = 0;
        std::size_t directOperand = 0;

        for (std::size_t opIdx = 0; opIdx < instr.operands.size(); ++opIdx) {
            const auto [isUse, isDef] = operandRoles(instr, opIdx);
            if (!isUse)
                continue;
            if (!operandUsesReg(instr.operands[opIdx], dst))
                continue;

            ++useCount;
            if (const auto *opReg = asReg(instr.operands[opIdx]); opReg && !isDef) {
                ++directUseCount;
                directOperand = opIdx;
            }
        }

        if (useCount != 0) {
            if (crossedCall)
                return std::nullopt;
            if (useCount != 1 || directUseCount != 1 || definesReg(instr, dst))
                return std::nullopt;
            if (site)
                return std::nullopt;
            site = UseSite{idx, directOperand};
        }

        if (definesReg(instr, dst))
            break;
        if (instr.opcode == MOpcode::CALL)
            crossedCall = true;
        if (isBlockBoundary(instr.opcode) && instr.opcode != MOpcode::CALL)
            break;
    }

    return site;
}

std::size_t rewriteSingleUseCopies(MBasicBlock &block) {
    std::vector<bool> erase(block.instructions.size(), false);
    std::size_t removed = 0;

    for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
        auto &instr = block.instructions[idx];
        if (!isCopyOpcode(instr.opcode) || instr.operands.size() != 2)
            continue;

        const auto *dst = asReg(instr.operands[0]);
        const auto *src = asReg(instr.operands[1]);
        // Physical sources such as ABI return registers are not tracked as
        // live ranges here. Forwarding them would let register allocation
        // reuse that physical register before the forwarded use.
        if (!dst || !src || dst->isPhys || src->isPhys || dst->cls != src->cls ||
            sameReg(*dst, *src))
            continue;

        auto site = findSingleDirectUse(block, idx, *dst, *src);
        if (!site)
            continue;

        block.instructions[site->instrIndex].operands[site->operandIndex] = instr.operands[1];
        erase[idx] = true;
        ++removed;
    }

    if (removed == 0)
        return 0;

    std::vector<MInstr> kept;
    kept.reserve(block.instructions.size() - removed);
    for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
        if (!erase[idx])
            kept.push_back(std::move(block.instructions[idx]));
    }
    block.instructions = std::move(kept);
    return removed;
}

} // namespace

std::size_t runPreRegAllocOpt(MFunction &fn) {
    std::size_t removed = 0;
    for (auto &block : fn.blocks) {
        const auto oldSize = block.instructions.size();
        block.instructions.erase(std::remove_if(block.instructions.begin(),
                                                block.instructions.end(),
                                                isIdentityCopy),
                                 block.instructions.end());
        removed += oldSize - block.instructions.size();
        removed += rewriteSingleUseCopies(block);
    }
    return removed;
}

} // namespace viper::codegen::x64
