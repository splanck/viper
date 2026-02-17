//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/Inline.hpp
// Purpose: Lightweight direct-call inliner module pass with a configurable
//          cost model (instruction/block budgets, constant-argument bonus,
//          single-use bonus, code-growth cap, inline-depth limit). Clones
//          callee CFG into caller, remaps params, rewires returns.
// Key invariants:
//   - Recursive calls are never inlined.
//   - EH-sensitive callees are skipped.
//   - Total code growth is capped by maxCodeGrowth.
// Ownership/Lifetime: ModulePass instantiated by the registry; configuration
//          is held by value in InlineCostConfig.
// Links: il/transform/PassRegistry.hpp, il/analysis/CallGraph.hpp
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
    /// Raised from 32 to 80 to capture medium-sized helper functions.
    unsigned instrThreshold = 80;

    /// Maximum number of blocks in callee.
    /// Raised from 4 to 8 to allow inlining functions with conditional branches.
    unsigned blockBudget = 8;

    /// Maximum inline depth for nested inlining.
    /// Raised from 2 to 3 to allow deeper utility-function chains to collapse.
    unsigned maxInlineDepth = 3;

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

/// @brief Direct-call inliner module pass with a configurable cost model.
/// @details Scans for direct call sites that satisfy the cost model thresholds
///          (instruction budget, block budget, constant-argument bonuses, etc.),
///          clones the callee's CFG into the caller, remaps parameters to
///          arguments, and rewires return values. Recursive and EH-sensitive
///          callees are always skipped. Total code growth across the module is
///          capped by @ref InlineCostConfig::maxCodeGrowth.
class Inliner : public ModulePass
{
  public:
    /// @brief Construct an inliner with default cost configuration.
    Inliner() = default;

    /// @brief Construct an inliner with a custom cost configuration.
    /// @param config Cost model thresholds controlling inlining decisions.
    explicit Inliner(InlineCostConfig config) : config_(config) {}

    /// @brief Return the pass identifier string ("inline").
    std::string_view id() const override;

    /// @brief Run the inliner over all functions in @p module.
    /// @param module Module containing functions to consider for inlining.
    /// @param analysis Analysis manager providing call graph and dominators.
    /// @return PreservedAnalyses indicating which analyses remain valid.
    PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) override;

    /// @brief Override the instruction count threshold for inlining decisions.
    /// @param n New threshold (callee instructions must not exceed this).
    void setInstructionThreshold(unsigned n)
    {
        config_.instrThreshold = n;
    }

    /// @brief Replace the entire cost configuration.
    /// @param config New cost model configuration to use.
    void setConfig(InlineCostConfig config)
    {
        config_ = config;
    }

  private:
    InlineCostConfig config_;
};

/// @brief Register the inliner pass with the provided registry.
/// @param registry PassRegistry to register the pass into.
void registerInlinePass(PassRegistry &registry);

} // namespace il::transform
