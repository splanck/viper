//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Induction Variable Simplification + Loop Strength Reduction — Function Pass
//
// This pass normalizes simple counted loops and performs basic strength
// reduction for linear expressions of an induction variable. It prefers
// canonical forms like `i < bound` with positive steps, and rewrites repeated
// computations `base + i * stride` into incremental updates of a
// loop-carried temporary by hoisting the initial value into the preheader and
// passing the value via block parameters.
//
// The implementation is deliberately conservative and targets single-latch,
// well-structured loops discovered by LoopInfo.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

class IndVarSimplify : public FunctionPass
{
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

/// Register the IndVarSimplify function pass under identifier "indvars".
void registerIndVarSimplifyPass(PassRegistry &registry);

} // namespace il::transform
