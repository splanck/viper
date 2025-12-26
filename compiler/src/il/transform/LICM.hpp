//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Loop-Invariant Code Motion (LICM) optimization pass.
// LICM moves loop-invariant computations from inside loops to preheader blocks,
// reducing redundant computation on each loop iteration and improving performance.
//
// When a loop repeatedly computes the same value that doesn't depend on the loop
// iteration, that computation can be moved outside the loop to execute only once.
// LICM identifies such loop-invariant instructions (those whose operands are
// either loop-invariant or defined outside the loop) and hoists them to loop
// preheaders, maintaining program semantics while reducing execution cost.
//
// Key Responsibilities:
// - Identify natural loops within functions using loop analysis
// - Detect loop-invariant instructions whose operands don't change per iteration
// - Hoist eligible invariant instructions to loop preheaders
// - Preserve program semantics (only hoist when provably safe)
// - Maintain SSA form and dominance relationships
//
// Design Notes:
// The pass operates conservatively, hoisting only instructions with no side
// effects and whose results are guaranteed to be computed on all loop iterations.
// It requires loop structure analysis (identifying headers, preheaders, latches)
// and uses dataflow information to determine invariance. The implementation works
// with the pass manager's analysis caching system to efficiently query loop
// structure without recomputation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Perform loop-invariant code motion for trivially safe instructions.
/// @details Hoists instructions whose operands are loop-invariant, whose opcode
///          is side-effect free and non-trapping, and (for loads) only when the
///          loop contains no memory writes (based on BasicAA/modref metadata).
///          Assumes LoopSimplify has provided a dedicated preheader/latch.
class LICM : public FunctionPass
{
  public:
    /// @brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// @brief Run loop-invariant code motion over @p function.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

/// @brief Register the LICM pass with the provided registry.
void registerLICMPass(PassRegistry &registry);

} // namespace il::transform
