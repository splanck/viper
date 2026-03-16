//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/MovFolding.cpp
// Purpose: Move folding peephole sub-pass for the x86-64 backend.
//          Folds consecutive register-to-register moves when the intermediate
//          register is dead.
//
// Key invariants:
//   - Only folds when r1 is not used after the second move in the same block.
//   - Argument registers near calls are not folded to avoid ABI violations.
//
// Ownership/Lifetime:
//   - Stateless; all state is owned by the caller.
//
// Links: src/codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "MovFolding.hpp"

namespace viper::codegen::x64::peephole
{

bool tryFoldConsecutiveMoves(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats)
{
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
    if (isArgReg(r1))
    {
        // Check for call instructions after the second move
        for (std::size_t i = idx + 2; i < instrs.size(); ++i)
        {
            if (instrs[i].opcode == MOpcode::CALL)
                return false; // Call instruction found, don't fold
            if (definesReg(instrs[i], r1))
                break; // r1 is redefined before any call, safe to check further
        }
    }

    // Check that r1 is not used after second in this block
    for (std::size_t i = idx + 2; i < instrs.size(); ++i)
    {
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

} // namespace viper::codegen::x64::peephole
