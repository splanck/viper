//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the PipelineExecutor class, which coordinates execution of
// optimization pass pipelines on IL modules. The executor resolves pass names to
// registered pass instances, manages analysis caching and invalidation, and provides
// instrumentation hooks for debugging and verification.
//
// A pass pipeline is an ordered sequence of transformation and analysis passes.
// The PipelineExecutor takes a pipeline specification (list of pass names), looks
// up each pass in the registry, instantiates pass objects, executes them in order,
// and maintains analysis results between passes based on preservation information.
// Instrumentation hooks enable printing IR before/after passes and verifying
// correctness after each transformation.
//
// Key Responsibilities:
// - Resolve pass names to registered pass factories
// - Instantiate module and function passes for pipeline execution
// - Execute passes in specified order on the module
// - Manage analysis caching and invalidation based on PreservedAnalyses
// - Invoke instrumentation hooks (print before/after, verification)
// - Coordinate between module-level and function-level passes
//
// Design Notes:
// The executor is configured with const references to registries (passes and
// analyses) and instrumentation callbacks. It doesn't own the registries, only
// borrows them during pipeline execution. The executor creates a single
// AnalysisManager per module for the pipeline run, enabling analysis result
// sharing across passes. The instrumentation structure allows customizable
// debugging without coupling the executor to specific output mechanisms.
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
                     Instrumentation instrumentation);

    void run(core::Module &module, const std::vector<std::string> &pipeline) const;

  private:
    const PassRegistry &registry_;
    const AnalysisRegistry &analysisRegistry_;
    Instrumentation instrumentation_;
};

} // namespace il::transform
