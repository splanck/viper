//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/IdentityElim.cpp
// Purpose: Identity move elimination and consecutive move folding for
//          the AArch64 peephole optimizer.
//
// Key invariants:
//   - Identity move removal only removes provably redundant mov r,r / fmov d,d.
//   - Consecutive move folding checks liveness before transforming.
//
// Ownership/Lifetime:
//   - Operates on mutable instruction vectors owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "IdentityElim.hpp"

#include "PeepholeCommon.hpp"

namespace viper::codegen::aarch64::peephole {

bool isIdentityMovRR(const MInstr &instr) noexcept {
    if (instr.opc != MOpcode::MovRR)
        return false;
    if (instr.ops.size() != 2)
        return false;
    return samePhysReg(instr.ops[0], instr.ops[1]);
}

bool isIdentityFMovRR(const MInstr &instr) noexcept {
    if (instr.opc != MOpcode::FMovRR)
        return false;
    if (instr.ops.size() != 2)
        return false;
    return samePhysReg(instr.ops[0], instr.ops[1]);
}

bool tryFoldConsecutiveMoves(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats) {
    if (idx + 1 >= instrs.size())
        return false;

    MInstr &first = instrs[idx];
    MInstr &second = instrs[idx + 1];

    const bool firstIsMovRR = (first.opc == MOpcode::MovRR && first.ops.size() == 2);
    const bool secondIsMovRR = (second.opc == MOpcode::MovRR && second.ops.size() == 2);

    if (!firstIsMovRR || !secondIsMovRR)
        return false;

    if (!samePhysReg(second.ops[1], first.ops[0]))
        return false;

    const MOperand &r1 = first.ops[0];
    if (isArgReg(r1)) {
        for (std::size_t i = idx + 2; i < instrs.size(); ++i) {
            if (instrs[i].opc == MOpcode::Bl || instrs[i].opc == MOpcode::Blr)
                return false;
            if (definesReg(instrs[i], r1))
                break;
        }
    }

    for (std::size_t i = idx + 2; i < instrs.size(); ++i) {
        if (usesReg(instrs[i], r1))
            return false;
        if (definesReg(instrs[i], r1))
            break;
    }

    const MOperand originalSrc = first.ops[1];
    second.ops[1] = originalSrc;
    first.ops[0] = first.ops[1];
    ++stats.consecutiveMovsFolded;
    return true;
}

bool tryFoldImmThenMove(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats) {
    if (idx + 1 >= instrs.size())
        return false;

    MInstr &first = instrs[idx];
    MInstr &second = instrs[idx + 1];

    if (first.opc != MOpcode::MovRI || first.ops.size() != 2)
        return false;
    if (second.opc != MOpcode::MovRR || second.ops.size() != 2)
        return false;
    if (!samePhysReg(second.ops[1], first.ops[0]))
        return false;

    const MOperand &rd = first.ops[0];
    if (isArgReg(rd)) {
        for (std::size_t i = idx + 2; i < instrs.size(); ++i) {
            if (instrs[i].opc == MOpcode::Bl || instrs[i].opc == MOpcode::Blr)
                return false;
            if (definesReg(instrs[i], rd))
                break;
        }
    }

    for (std::size_t i = idx + 2; i < instrs.size(); ++i) {
        if (usesReg(instrs[i], rd))
            return false;
        if (definesReg(instrs[i], rd))
            break;
    }

    const MOperand imm = first.ops[1];
    first.opc = MOpcode::MovRR;
    first.ops[1] = first.ops[0];
    second.opc = MOpcode::MovRI;
    second.ops[1] = imm;
    ++stats.consecutiveMovsFolded;
    return true;
}

} // namespace viper::codegen::aarch64::peephole
