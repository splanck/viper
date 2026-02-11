//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/PipelineExecutor.hpp
// Purpose: Coordinates execution of optimisation pass pipelines on IL modules.
//          Resolves pass names to registered factories, manages analysis
//          caching/invalidation via AnalysisManager, and invokes
//          instrumentation hooks (IR printing, verification, metrics).
// Key invariants:
//   - Pass and analysis registries are borrowed by const reference and must
//     outlive the executor.
//   - A single AnalysisManager is created per pipeline run.
// Ownership/Lifetime: PipelineExecutor borrows registries and instrumentation
//          callbacks; does not own them. Caller owns the Module.
// Links: il/transform/PassRegistry.hpp, il/transform/AnalysisManager.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/PassRegistry.hpp"
#include "viper/pass/PassManager.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace il::transform
{

class PipelineExecutor
{
  public:
    struct PassMetrics
    {
        struct IRSize
        {
            std::size_t blocks = 0;
            std::size_t instructions = 0;
        };

        IRSize before;
        IRSize after;
        AnalysisCounts analysesComputed{};
        std::chrono::nanoseconds duration{};
    };

    /// @brief Configuration for instrumentation hooks around pass execution.
    struct Instrumentation
    {
        viper::pass::PassManager::PrintHook printBefore;
        viper::pass::PassManager::PrintHook printAfter;
        viper::pass::PassManager::VerifyHook verifyEach;
        std::function<void(std::string_view id, const PassMetrics &metrics)> passMetrics;
    };

    PipelineExecutor(const PassRegistry &registry,
                     const AnalysisRegistry &analysisRegistry,
                     Instrumentation instrumentation,
                     bool parallelFunctionPasses = false);

    void run(core::Module &module, const std::vector<std::string> &pipeline) const;

  private:
    const PassRegistry &registry_;
    const AnalysisRegistry &analysisRegistry_;
    Instrumentation instrumentation_;
    bool parallelFunctionPasses_ = false;
};

} // namespace il::transform
