//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/LoopUnroll.hpp
// Purpose: Loop Unrolling function pass -- replicates loop bodies to reduce
//          iteration overhead. Supports full unrolling (small constant trip
//          count) and optional partial unrolling. Configurable via
//          LoopUnrollConfig thresholds.
// Key invariants:
//   - Only unrolls single-latch, single-exit loops without nesting.
//   - Full unrolling limited by fullUnrollThreshold to prevent code bloat.
//   - Block parameters (SSA phi equivalents) are threaded correctly across
//     unrolled iterations.
// Ownership/Lifetime: FunctionPass holding a LoopUnrollConfig by value;
//          instantiated by the registry.
// Links: il/transform/PassRegistry.hpp, il/transform/analysis/LoopInfo.hpp
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
