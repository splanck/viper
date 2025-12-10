//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/Peephole.cpp
// Purpose: Implement conservative peephole optimizations over Machine IR for
//          the AArch64 backend.
//
// Key invariants:
// - Rewrites preserve instruction ordering and only substitute encodings that
//   are provably equivalent under the Machine IR conventions.
// - Must be called after register allocation when physical registers are known.
//
// Ownership/Lifetime:
// - Mutates Machine IR graphs owned by the caller without retaining references
//   to transient operands.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Peephole optimization pass for the AArch64 code generator.
/// @details Implements local rewrites that eliminate redundant moves and
///          fold consecutive register-to-register operations. The patterns
///          implemented are conservative and safe to apply after register
///          allocation.

#include "Peephole.hpp"

#include <algorithm>

namespace viper::codegen::aarch64
{
namespace
{

/// @brief Check if an operand is a physical register.
///
/// @param op Operand to check.
/// @return true if the operand is a physical register.
[[nodiscard]] bool isPhysReg(const MOperand &op) noexcept
{
    return op.kind == MOperand::Kind::Reg && op.reg.isPhys;
}

/// @brief Check if two register operands refer to the same physical register.
///
/// @param a First operand.
/// @param b Second operand.
/// @return true if both are physical registers with the same class and ID.
[[nodiscard]] bool samePhysReg(const MOperand &a, const MOperand &b) noexcept
{
    if (!isPhysReg(a) || !isPhysReg(b))
        return false;
    return a.reg.cls == b.reg.cls && a.reg.idOrPhys == b.reg.idOrPhys;
}

/// @brief Check if an instruction is an identity move (mov r, r).
///
/// @param instr Instruction to check.
/// @return true if the instruction is MovRR with identical source and destination.
[[nodiscard]] bool isIdentityMovRR(const MInstr &instr) noexcept
{
    if (instr.opc != MOpcode::MovRR)
        return false;
    if (instr.ops.size() != 2)
        return false;
    return samePhysReg(instr.ops[0], instr.ops[1]);
}

/// @brief Check if an instruction is an identity FPR move (fmov d, d).
///
/// @param instr Instruction to check.
/// @return true if the instruction is FMovRR with identical source and destination.
[[nodiscard]] bool isIdentityFMovRR(const MInstr &instr) noexcept
{
    if (instr.opc != MOpcode::FMovRR)
        return false;
    if (instr.ops.size() != 2)
        return false;
    return samePhysReg(instr.ops[0], instr.ops[1]);
}

/// @brief Check if an instruction defines a given physical register.
///
/// @param instr Instruction to check.
/// @param reg Register to look for.
/// @return true if the instruction writes to the given register.
[[nodiscard]] bool definesReg(const MInstr &instr, const MOperand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    // Most AArch64 instructions have the destination as the first operand.
    // Check common patterns.
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::Cset:
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::AddFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::MSubRRRR:
            if (!instr.ops.empty() && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        // Instructions that don't define registers
        case MOpcode::CmpRR:
        case MOpcode::CmpRI:
        case MOpcode::FCmpRR:
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Bl:
        case MOpcode::Ret:
        case MOpcode::Cbz:
        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
        case MOpcode::SubSpImm:
        case MOpcode::AddSpImm:
            break;
    }
    return false;
}

/// @brief Check if an instruction uses a given physical register as a source.
///
/// @param instr Instruction to check.
/// @param reg Register to look for.
/// @return true if the instruction reads from the given register.
[[nodiscard]] bool usesReg(const MInstr &instr, const MOperand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    // Check source operands (typically starting from index 1 for most ops)
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::FMovRR:
            // dst, src - check src (index 1)
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
            // dst, lhs, rhs - check lhs and rhs (indices 1, 2)
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            if (instr.ops.size() >= 3 && samePhysReg(instr.ops[2], reg))
                return true;
            break;

        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
            // dst, src, imm - check src (index 1)
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::CmpRR:
        case MOpcode::FCmpRR:
            // lhs, rhs - check both
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::CmpRI:
            // src, imm - check src
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
            // src, offset - check src
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
            // src, base, offset - check src and base
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
            // dst, base, offset - check base
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::Cbz:
            // reg, label - check reg
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
            // dst, src - check src
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::MSubRRRR:
            // dst, mul1, mul2, sub - check mul1, mul2, sub
            for (std::size_t i = 1; i < instr.ops.size() && i <= 3; ++i)
            {
                if (samePhysReg(instr.ops[i], reg))
                    return true;
            }
            break;

        case MOpcode::AddPageOff:
            // dst, base, label - check base
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        default:
            break;
    }
    return false;
}

/// @brief Check if a register is an argument-passing register (x0-x7).
///
/// @param reg Register to check.
/// @return true if the register is one of x0-x7.
[[nodiscard]] bool isArgReg(const MOperand &reg) noexcept
{
    if (!isPhysReg(reg) || reg.reg.cls != RegClass::GPR)
        return false;
    const auto pr = static_cast<PhysReg>(reg.reg.idOrPhys);
    return pr >= PhysReg::X0 && pr <= PhysReg::X7;
}

/// @brief Try to fold consecutive moves: mov r1, r2; mov r3, r1 -> mov r3, r2
///
/// This optimization is only safe when r1 is not used after the second move
/// within the same basic block, and when there are no intervening calls that
/// might implicitly use argument registers.
///
/// @param instrs Vector of instructions in a basic block.
/// @param idx Index of the first instruction to check.
/// @return true if a fold was performed.
[[nodiscard]] bool tryFoldConsecutiveMoves(std::vector<MInstr> &instrs,
                                           std::size_t idx,
                                           PeepholeStats &stats)
{
    if (idx + 1 >= instrs.size())
        return false;

    MInstr &first = instrs[idx];
    MInstr &second = instrs[idx + 1];

    // Check for: mov r1, r2; mov r3, r1
    const bool firstIsMovRR = (first.opc == MOpcode::MovRR && first.ops.size() == 2);
    const bool secondIsMovRR = (second.opc == MOpcode::MovRR && second.ops.size() == 2);

    if (!firstIsMovRR || !secondIsMovRR)
        return false;

    // first: dst=r1, src=r2
    // second: dst=r3, src=r1
    // Check if second.src == first.dst
    if (!samePhysReg(second.ops[1], first.ops[0]))
        return false;

    // Don't fold if the intermediate register is an argument register and
    // there's a call instruction nearby. Call instructions implicitly use
    // argument registers, which our simple liveness check doesn't detect.
    const MOperand &r1 = first.ops[0];
    if (isArgReg(r1))
    {
        // Check for call instructions after the second move
        for (std::size_t i = idx + 2; i < instrs.size(); ++i)
        {
            if (instrs[i].opc == MOpcode::Bl)
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
    const MOperand originalSrc = first.ops[1]; // Save the original source
    second.ops[1] = originalSrc;               // second.src = first.src
    first.ops[0] = first.ops[1];               // Make first identity (mov r2, r2)
    ++stats.consecutiveMovsFolded;
    return true;
}

/// @brief Remove instructions marked for deletion from a basic block.
///
/// @param instrs Vector of instructions to filter.
/// @param toRemove Set of indices to remove.
void removeMarkedInstructions(std::vector<MInstr> &instrs,
                              const std::vector<bool> &toRemove)
{
    std::size_t writeIdx = 0;
    for (std::size_t readIdx = 0; readIdx < instrs.size(); ++readIdx)
    {
        if (!toRemove[readIdx])
        {
            if (writeIdx != readIdx)
                instrs[writeIdx] = std::move(instrs[readIdx]);
            ++writeIdx;
        }
    }
    instrs.resize(writeIdx);
}

} // namespace

PeepholeStats runPeephole(MFunction &fn)
{
    PeepholeStats stats;

    for (auto &block : fn.blocks)
    {
        auto &instrs = block.instrs;
        if (instrs.empty())
            continue;

        // Pass 1: Try to fold consecutive moves
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            tryFoldConsecutiveMoves(instrs, i, stats);
        }

        // Pass 2: Mark identity moves for removal
        std::vector<bool> toRemove(instrs.size(), false);

        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            if (isIdentityMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            }
            else if (isIdentityFMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityFMovesRemoved;
            }
        }

        // Pass 3: Remove marked instructions
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; }))
        {
            removeMarkedInstructions(instrs, toRemove);
        }
    }

    return stats;
}

} // namespace viper::codegen::aarch64
