//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the CheckOpt pass for optimizing check opcodes such as
// IdxChk, SDivChk0, UDivChk0, SRemChk0, URemChk0, and cast checks.
//
// CheckOpt performs two primary optimizations:
//
// 1. Dominance-based Redundancy Elimination:
//    When a check instruction with identical opcode and operands has already
//    executed along all paths to the current check (i.e., the prior check
//    dominates the current one), the current check is redundant and can be
//    removed. Uses of the current check's result are redirected to the
//    dominating check's result.
//
// 2. Loop-Invariant Check Hoisting:
//    When a check instruction's operands are all defined outside a loop (or
//    are themselves loop-invariant), the check can be hoisted to the loop
//    preheader. This ensures the check executes once before the loop rather
//    than on every iteration, reducing overhead while preserving semantics.
//
// The pass operates conservatively: checks are only removed when provably
// redundant, and hoisting only occurs when the check would execute on every
// loop entry regardless of control flow within the loop.
//
// Design Notes:
// - Uses dominator tree for redundancy detection via preorder traversal
// - Uses LoopInfo to identify loops and determine operand invariance
// - Tracks check conditions via a lightweight key combining opcode and operands
// - Preserves CFG structure and dominance relationships when only removing
//   instructions; invalidates analyses when moving instructions
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Optimize check opcodes via redundancy elimination and loop hoisting.
///
/// This pass identifies check instructions (IdxChk, SDivChk0, UDivChk0, etc.)
/// that are redundant due to dominating equivalent checks, or that can be
/// safely hoisted out of loops when their operands are loop-invariant.
class CheckOpt : public FunctionPass
{
  public:
    /// @brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// @brief Run check optimization over @p function.
    /// @param function Function to optimize.
    /// @param analysis Analysis manager for querying dominators and loop info.
    /// @return PreservedAnalyses indicating which analyses remain valid.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

/// @brief Register the CheckOpt pass with the provided registry.
/// @param registry PassRegistry to register the pass into.
void registerCheckOptPass(PassRegistry &registry);

} // namespace il::transform
