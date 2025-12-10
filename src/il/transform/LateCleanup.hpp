//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the LateCleanup pass, a thin wrapper around SimplifyCFG
// and DCE tuned for late-pipeline cleanup. The pass now runs a bounded
// fixpoint: multiple iterations of CFG simplification followed by dead code
// elimination, stopping early when no further reduction is observed. A small
// stats hook records IL size before/after so callers can inspect how much
// cleanup occurred.
//
// LateCleanup is designed to run at the tail of the O2 pipeline to clean up
// trivial dead code and CFG noise created by earlier passes (inlining, GVN,
// check-opt, etc.).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <vector>

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Aggregate size information collected while running LateCleanup.
struct LateCleanupStats
{
    unsigned iterations = 0;
    std::size_t instrBefore = 0;
    std::size_t blocksBefore = 0;
    std::size_t instrAfter = 0;
    std::size_t blocksAfter = 0;
    std::vector<std::size_t> instrPerIter;
    std::vector<std::size_t> blocksPerIter;
};

/// @brief Late-pipeline cleanup pass combining CFG simplification and DCE.
/// @details Runs a bounded fixpoint of SimplifyCFG then DCE, tracking IL size
///          per iteration. This is a cheap cleanup pass designed for the tail
///          of optimization pipelines; iteration count is capped to avoid
///          unbounded work while still converging on a small fixpoint.
class LateCleanup : public ModulePass
{
  public:
    /// @brief Optionally attach a stats sink to observe size deltas.
    /// @param stats Struct that will be populated during @ref run.
    void setStats(LateCleanupStats *stats) { stats_ = stats; }

    /// @brief Get the unique identifier for this pass.
    /// @return String view "late-cleanup".
    std::string_view id() const override;

    /// @brief Execute the late cleanup pass on the module.
    /// @param module Module to transform.
    /// @param analysis Analysis manager for querying cached results.
    /// @return PreservedAnalyses indicating which analyses remain valid.
    PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) override;

  private:
    LateCleanupStats *stats_ = nullptr;
};

/// @brief Register the late cleanup pass with the registry.
/// @param registry PassRegistry to register the pass into.
void registerLateCleanupPass(PassRegistry &registry);

} // namespace il::transform
