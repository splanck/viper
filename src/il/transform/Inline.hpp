//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// A lightweight direct-call inliner with an enhanced cost model that considers
// both the cost and benefit of inlining decisions.
//
// The pass targets small callees (instruction budget + block budget), avoids
// recursion, and skips exception-handling sensitive callees. Supported control
// flow: direct calls without EH, `br`/`cbr`/`switch`/`ret`, and block params.
//
// Cost Model Features:
// - Base instruction/block budgets (configurable thresholds)
// - Call frequency analysis (inline hot callees more aggressively)
// - Constant argument bonus (enables more optimization after inlining)
// - Single-use function bonus (can be deleted after inlining)
// - Code growth tracking (limits total expansion)
// - Inline depth limiting (prevents excessive nesting)
//
// Inlining clones the callee CFG into the caller, remaps callee params to call
// operands (including block parameters), rewires returns to a continuation
// block, and assigns fresh SSA temporaries for all cloned results.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Configuration for the inline cost model.
struct InlineCostConfig
{
    /// Base instruction count threshold for inlining.
    unsigned instrThreshold = 32;

    /// Maximum number of blocks in callee.
    unsigned blockBudget = 4;

    /// Maximum inline depth for nested inlining.
    unsigned maxInlineDepth = 2;

    /// Bonus (subtracted from cost) for each constant argument.
    unsigned constArgBonus = 4;

    /// Bonus for functions with only one call site (can be DCE'd after).
    unsigned singleUseBonus = 10;

    /// Bonus for very small functions (<=8 instructions).
    unsigned tinyFunctionBonus = 16;

    /// Maximum total instruction count growth allowed per module.
    unsigned maxCodeGrowth = 1000;

    /// Enable aggressive inlining mode.
    bool aggressive = false;
};

class Inliner : public ModulePass
{
  public:
    Inliner() = default;

    explicit Inliner(InlineCostConfig config) : config_(config) {}

    std::string_view id() const override;
    PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) override;

    void setInstructionThreshold(unsigned n)
    {
        config_.instrThreshold = n;
    }

    void setConfig(InlineCostConfig config)
    {
        config_ = config;
    }

  private:
    InlineCostConfig config_;
};

void registerInlinePass(PassRegistry &registry);

} // namespace il::transform
