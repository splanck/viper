//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the LateCleanup pass, a thin wrapper around SimplifyCFG
// and DCE tuned for late-pipeline cleanup. The pass runs one or two iterations
// of CFG simplification followed by dead code elimination to achieve a
// "fixpoint-ish" cleanup without being too expensive.
//
// LateCleanup is designed to run at the tail of the O2 pipeline to clean up
// trivial dead code and CFG noise created by earlier passes (inlining, GVN,
// check-opt, etc.).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Late-pipeline cleanup pass combining CFG simplification and DCE.
/// @details Runs limited iterations of SimplifyCFG and DCE to clean up
///          artifacts from earlier optimization passes. This is a cheap
///          cleanup pass designed to be run at the end of optimization
///          pipelines.
class LateCleanup : public ModulePass
{
  public:
    /// @brief Get the unique identifier for this pass.
    /// @return String view "late-cleanup".
    std::string_view id() const override;

    /// @brief Execute the late cleanup pass on the module.
    /// @param module Module to transform.
    /// @param analysis Analysis manager for querying cached results.
    /// @return PreservedAnalyses indicating which analyses remain valid.
    PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) override;
};

/// @brief Register the late cleanup pass with the registry.
/// @param registry PassRegistry to register the pass into.
void registerLateCleanupPass(PassRegistry &registry);

} // namespace il::transform
