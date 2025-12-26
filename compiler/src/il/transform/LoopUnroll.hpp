//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Loop Unrolling optimization pass for IL functions.
// Loop unrolling reduces loop overhead by replicating the loop body multiple
// times, decreasing the number of branch instructions and enabling additional
// optimization opportunities through increased instruction-level parallelism.
//
// Unrolling is particularly effective for small loops with known trip counts,
// where the overhead of iteration (increment, comparison, branch) is significant
// relative to the loop body work. The pass supports two modes:
// - Full unrolling: Completely eliminates the loop when trip count is small
// - Partial unrolling: Replicates the body a fixed number of times, keeping
//   the loop structure but with fewer iterations
//
// Key Responsibilities:
// - Identify loops suitable for unrolling (simple structure, known trip count)
// - Determine optimal unroll factor based on loop size and trip count
// - Replicate loop body with proper SSA value renaming
// - Update loop-carried values (block parameters) across unrolled iterations
// - Maintain program semantics through correct control flow updates
//
// Design Notes:
// The pass operates conservatively, only unrolling loops that meet strict
// criteria: single latch, single exit, no nested loops, and either a known
// constant trip count or simple induction variable pattern. Full unrolling
// is limited to small trip counts (configurable threshold) to prevent code
// size explosion. The implementation properly handles block parameters (SSA
// phi equivalents) by threading values through unrolled iterations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Configuration parameters for loop unrolling.
struct LoopUnrollConfig
{
    /// Maximum trip count for full unrolling (eliminates loop entirely).
    unsigned fullUnrollThreshold = 8;

    /// Maximum loop body size (instructions) for unrolling consideration.
    unsigned maxLoopSize = 50;

    /// Unroll factor for partial unrolling when full unroll is not applicable.
    unsigned partialUnrollFactor = 4;

    /// Whether to enable partial unrolling (in addition to full unrolling).
    bool enablePartialUnroll = false;
};

/// @brief Loop unrolling optimization pass.
/// @details Unrolls small constant-bound loops to reduce iteration overhead
///          and expose optimization opportunities. The pass identifies loops
///          with known trip counts and replicates their bodies, updating
///          SSA values appropriately.
class LoopUnroll : public FunctionPass
{
  public:
    explicit LoopUnroll(LoopUnrollConfig config = {}) : config_(config) {}

    /// @brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// @brief Run loop unrolling over @p function.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;

  private:
    LoopUnrollConfig config_;
};

/// @brief Register the loop unrolling pass with the provided registry.
void registerLoopUnrollPass(PassRegistry &registry);

} // namespace il::transform
