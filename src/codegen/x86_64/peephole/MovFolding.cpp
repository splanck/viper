//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/peephole/MovFolding.cpp
// Purpose: Move folding peephole sub-pass for the x86-64 backend.
//          Folds consecutive register-to-register moves when the intermediate
//          register is dead.
// Key invariants:
//   - Only folds when r1 is not used after the second move in the same block.
//   - Argument registers near calls are not folded to avoid ABI violations.
// Ownership/Lifetime:
//   - Stateless; all state is owned by the caller.
// Links: codegen/x86_64/peephole/MovFolding.hpp,
//        codegen/x86_64/peephole/PeepholeCommon.hpp
//
//===----------------------------------------------------------------------===//

#include "MovFolding.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <cstdint>
#include <vector>

namespace viper::codegen::x64::peephole {

namespace {

using RegMask = uint64_t;

[[nodiscard]] RegMask regBit(uint16_t reg) noexcept {
    return reg < 64 ? (RegMask{1} << reg) : RegMask{0};
}

void addOperandReg(RegMask &mask, const Operand &op) noexcept {
    const auto *reg = std::get_if<OpReg>(&op);
    if (reg && reg->isPhys)
        mask |= regBit(reg->idOrPhys);
}

void addOperandMemRegs(RegMask &mask, const Operand &op) noexcept {
    const auto *mem = std::get_if<OpMem>(&op);
    if (!mem)
        return;
    if (mem->base.isPhys)
        mask |= regBit(mem->base.idOrPhys);
    if (mem->hasIndex && mem->index.isPhys)
        mask |= regBit(mem->index.idOrPhys);
}

[[nodiscard]] RegMask usedRegMask(const MInstr &instr) {
    RegMask mask = 0;
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isDef;
        if (!isUse)
            continue;
        addOperandReg(mask, instr.operands[idx]);
        addOperandMemRegs(mask, instr.operands[idx]);
    }

    switch (instr.opcode) {
        case MOpcode::RET:
            mask |= regBit(static_cast<uint16_t>(PhysReg::RAX));
            mask |= regBit(static_cast<uint16_t>(PhysReg::XMM0));
            mask |= regBit(static_cast<uint16_t>(PhysReg::RSP));
            break;
        case MOpcode::CQO:
            mask |= regBit(static_cast<uint16_t>(PhysReg::RAX));
            break;
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            mask |= regBit(static_cast<uint16_t>(PhysReg::RAX));
            mask |= regBit(static_cast<uint16_t>(PhysReg::RDX));
            break;
        case MOpcode::MULr:
        case MOpcode::IMULr:
            mask |= regBit(static_cast<uint16_t>(PhysReg::RAX));
            break;
        default:
            break;
    }

    return mask;
}

[[nodiscard]] RegMask defRegMask(const MInstr &instr) {
    RegMask mask = 0;
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isUse;
        if (!isDef)
            continue;
        addOperandReg(mask, instr.operands[idx]);
    }

    switch (instr.opcode) {
        case MOpcode::CQO:
            mask |= regBit(static_cast<uint16_t>(PhysReg::RDX));
            break;
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
        case MOpcode::MULr:
        case MOpcode::IMULr:
            mask |= regBit(static_cast<uint16_t>(PhysReg::RAX));
            mask |= regBit(static_cast<uint16_t>(PhysReg::RDX));
            break;
        default:
            break;
    }

    return mask;
}

[[nodiscard]] RegMask allArgRegMask() noexcept {
    return regBit(static_cast<uint16_t>(PhysReg::RDI)) |
           regBit(static_cast<uint16_t>(PhysReg::RSI)) |
           regBit(static_cast<uint16_t>(PhysReg::RDX)) |
           regBit(static_cast<uint16_t>(PhysReg::RCX)) |
           regBit(static_cast<uint16_t>(PhysReg::R8)) | regBit(static_cast<uint16_t>(PhysReg::R9));
}

[[nodiscard]] RegMask regOperandBit(const Operand &op) noexcept {
    const auto *reg = std::get_if<OpReg>(&op);
    if (!reg || !reg->isPhys)
        return 0;
    return regBit(reg->idOrPhys);
}

} // namespace

/// @brief Coalesce two adjacent reg-to-reg moves when the intermediate is dead.
/// @details Pattern: @c "MOV r1, r2; MOV r3, r1" — if @c r1 is not used past
///          the second move and isn't an argument register straddling a
///          subsequent CALL (which would violate the call ABI), we rewrite to
///          @c "MOV r3, r2" and reduce @c r1 to a self-move that the
///          identity-move pass will subsequently drop.
/// @param instrs Block instructions, mutated in place.
/// @param idx Index of the candidate first move.
/// @param stats Pass-wide statistics accumulator.
/// @return True when a fold was applied.
bool tryFoldConsecutiveMoves(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats) {
    if (idx + 1 >= instrs.size())
        return false;

    MInstr &first = instrs[idx];
    MInstr &second = instrs[idx + 1];

    // Check for: mov r1, r2; mov r3, r1
    const bool firstIsMovRR = (first.opcode == MOpcode::MOVrr && first.operands.size() == 2);
    const bool secondIsMovRR = (second.opcode == MOpcode::MOVrr && second.operands.size() == 2);

    if (!firstIsMovRR || !secondIsMovRR)
        return false;

    // first: dst=r1, src=r2
    // second: dst=r3, src=r1
    // Check if second.src == first.dst
    if (!samePhysReg(second.operands[1], first.operands[0]))
        return false;

    // Don't fold if the intermediate register is an argument register and
    // there's a call instruction nearby.
    const Operand &r1 = first.operands[0];
    if (isArgReg(r1)) {
        // Check for call instructions after the second move
        for (std::size_t i = idx + 2; i < instrs.size(); ++i) {
            if (instrs[i].opcode == MOpcode::CALL)
                return false; // Call instruction found, don't fold
            if (definesReg(instrs[i], r1))
                break; // r1 is redefined before any call, safe to check further
        }
    }

    // Check that r1 is not used after second in this block
    for (std::size_t i = idx + 2; i < instrs.size(); ++i) {
        if (usesReg(instrs[i], r1))
            return false; // r1 is still live, can't fold
        if (definesReg(instrs[i], r1))
            break; // r1 is redefined, safe to fold
    }

    // Perform the fold: second becomes mov r3, r2
    const Operand originalSrc = first.operands[1]; // Save the original source
    second.operands[1] = originalSrc;              // second.src = first.src
    first.operands[0] = first.operands[1];         // Make first identity (mov r2, r2)
    ++stats.consecutiveMovsFolded;
    return true;
}

std::size_t foldConsecutiveMoves(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    if (instrs.size() < 2)
        return 0;

    std::vector<RegMask> usedBeforeDefFrom(instrs.size() + 1, 0);
    std::vector<RegMask> callBeforeDefFrom(instrs.size() + 1, 0);

    RegMask usedBeforeDef = 0;
    RegMask callBeforeDef = 0;
    const RegMask argRegs = allArgRegMask();
    for (std::size_t i = instrs.size(); i-- > 0;) {
        const RegMask defs = defRegMask(instrs[i]);
        usedBeforeDef = (usedBeforeDef & ~defs) | usedRegMask(instrs[i]);
        callBeforeDef &= ~defs;
        if (instrs[i].opcode == MOpcode::CALL)
            callBeforeDef |= argRegs;
        usedBeforeDefFrom[i] = usedBeforeDef;
        callBeforeDefFrom[i] = callBeforeDef;
    }

    std::size_t folded = 0;
    for (std::size_t idx = 0; idx + 1 < instrs.size(); ++idx) {
        MInstr &first = instrs[idx];
        MInstr &second = instrs[idx + 1];

        const bool firstIsMovRR = (first.opcode == MOpcode::MOVrr && first.operands.size() == 2);
        const bool secondIsMovRR = (second.opcode == MOpcode::MOVrr && second.operands.size() == 2);
        if (!firstIsMovRR || !secondIsMovRR)
            continue;
        if (!samePhysReg(second.operands[1], first.operands[0]))
            continue;

        const Operand &r1 = first.operands[0];
        const RegMask r1Bit = regOperandBit(r1);
        if (r1Bit == 0)
            continue;

        const std::size_t suffixIndex = idx + 2;
        if ((usedBeforeDefFrom[suffixIndex] & r1Bit) != 0)
            continue;
        if (isArgReg(r1) && (callBeforeDefFrom[suffixIndex] & r1Bit) != 0)
            continue;

        const Operand originalSrc = first.operands[1];
        second.operands[1] = originalSrc;
        first.operands[0] = first.operands[1];
        ++stats.consecutiveMovsFolded;
        ++folded;
    }

    return folded;
}

} // namespace viper::codegen::x64::peephole
