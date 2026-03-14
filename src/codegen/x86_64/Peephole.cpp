//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Peephole.cpp
// Purpose: Driver for conservative peephole optimizations over Machine IR for
//          the x86-64 backend. Delegates to modular sub-passes under
//          peephole/*.cpp for each optimization category.
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
/// @brief Peephole optimization pass driver for the x86-64 code generator.
/// @details Orchestrates modular sub-passes that eliminate redundant moves,
///          fold consecutive register-to-register operations, apply strength
///          reduction, eliminate dead code, and optimize branch layout.

#include "Peephole.hpp"

#include "peephole/ArithSimplify.hpp"
#include "peephole/BranchOpt.hpp"
#include "peephole/DCE.hpp"
#include "peephole/MovFolding.hpp"
#include "peephole/PeepholeCommon.hpp"

#include <algorithm>

namespace viper::codegen::x64
{

// Import sub-pass functions into local scope for concise call sites.
namespace ph = peephole;

/// Maximum number of rewrite iterations before giving up on convergence.
/// Typical functions converge in 1-2 iterations; the bound guards against
/// pathological cases where rewrites keep enabling each other.
static constexpr std::size_t kMaxIterations = 100;

/// Run per-block rewrite passes (strength reduction, identity elimination,
/// move folding, DCE). Returns the number of transformations applied.
static std::size_t runBlockRewrites(MFunction &fn, ph::PeepholeStats &stats)
{
    std::size_t before = stats.total();

    for (auto &block : fn.blocks)
    {
        auto &instrs = block.instructions;
        if (instrs.empty())
            continue;

        // Pass 1: Build register constant map and apply rewrites
        ph::RegConstMap knownConsts;
        std::vector<bool> toRemove(instrs.size(), false);

        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            auto &instr = instrs[i];

            // Track constants loaded via MOVri
            ph::updateKnownConsts(instr, knownConsts);

            switch (instr.opcode)
            {
                case MOpcode::MOVri:
                {
                    if (instr.operands.size() != 2)
                        break;

                    if (!ph::isGprReg(instr.operands[0]) || !ph::isZeroImm(instr.operands[1]))
                        break;

                    // XOR clobbers EFLAGS; skip rewrite when a subsequent
                    // instruction reads flags before they are overwritten.
                    if (ph::nextInstrReadsFlags(instrs, i))
                        break;

                    ph::rewriteToXor(instr, instr.operands[0]);
                    ++stats.movZeroToXor;
                    break;
                }
                case MOpcode::CMPri:
                {
                    if (instr.operands.size() != 2)
                        break;

                    if (!ph::isGprReg(instr.operands[0]) || !ph::isZeroImm(instr.operands[1]))
                        break;

                    ph::rewriteToTest(instr, instr.operands[0]);
                    ++stats.cmpZeroToTest;
                    break;
                }
                default:
                    break;
            }

            // Try arithmetic identity elimination (add #0, shift #0)
            if (ph::tryArithmeticIdentity(instrs, i, stats))
            {
                toRemove[i] = true;
                continue;
            }

            // Try strength reduction (mul power-of-2 -> shift)
            (void)ph::tryStrengthReduction(instrs, i, knownConsts, stats);
        }

        // Pass 2: Try to fold consecutive moves
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            (void)ph::tryFoldConsecutiveMoves(instrs, i, stats);
        }

        // Pass 3: Mark identity moves for removal
        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            if (ph::isIdentityMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            }
            else if (ph::isIdentityMovSDRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            }
        }

        // Pass 4: Remove marked instructions
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; }))
        {
            ph::removeMarkedInstructions(instrs, toRemove);
        }

        // Pass 5: Dead code elimination
        ph::runBlockDCE(instrs, stats);
    }

    return stats.total() - before;
}

std::size_t runPeepholes(MFunction &fn)
{
    ph::PeepholeStats stats;

    // Iterate block rewrites to a fixed point. One rewrite can expose
    // further opportunities (e.g., strength reduction → identity move →
    // DCE), so iterate until no new transformations are found.
    for (std::size_t iter = 0; iter < kMaxIterations; ++iter)
    {
        if (runBlockRewrites(fn, stats) == 0)
            break;
    }

    // Layout and branch passes run once — they are idempotent and don't
    // benefit from iteration.

    // Pass 6: Greedy trace block layout
    ph::traceBlockLayout(fn, stats);

    // Pass 7: Cold block reordering
    ph::moveColdBlocks(fn, stats);

    // Pass 8: Branch chain elimination
    ph::eliminateBranchChains(fn, stats);

    // Pass 9: Conditional branch inversion
    ph::invertConditionalBranches(fn, stats);

    // Pass 10: Remove fallthrough jumps
    ph::removeFallthroughJumps(fn, stats);

    return stats.total();
}

} // namespace viper::codegen::x64
