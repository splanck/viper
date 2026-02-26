//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/PipelineExecutor.cpp
// Purpose: Define the executor that materialises IL transformation passes and
//          coordinates analysis invalidation between them.
// Links: docs/architecture.md#passes
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the stateful driver that runs pass pipelines.
/// @details Centralising the execution logic ensures registry lookups, analysis
///          invalidation, and optional verification follow a consistent policy
///          across all pipeline invocations.

#include "il/transform/PipelineExecutor.hpp"

#include "il/core/Module.hpp"
#include "viper/pass/PassManager.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace il::transform
{
namespace
{
PipelineExecutor::PassMetrics::IRSize computeIRSize(const core::Module &module)
{
    PipelineExecutor::PassMetrics::IRSize size{};
    for (const auto &fn : module.functions)
    {
        size.blocks += fn.blocks.size();
        for (const auto &block : fn.blocks)
            size.instructions += block.instructions.size();
    }
    return size;
}
} // namespace

/// @brief Construct an executor bound to specific pass and analysis registries.
/// @details Stores references to the pass and analysis registries plus a flag
///          controlling debug verification.  The executor itself remains
///          lightweight so pass managers can instantiate it per pipeline
///          invocation without sharing mutable state.
/// @param registry Pass registry supplying factories for module/function passes.
/// @param analysisRegistry Registry describing available analyses.
/// @param verifyBetweenPasses Controls whether debug builds run the verifier
///                            after each pass.
PipelineExecutor::PipelineExecutor(const PassRegistry &registry,
                                   const AnalysisRegistry &analysisRegistry,
                                   Instrumentation instrumentation,
                                   bool parallelFunctionPasses)
    : registry_(registry), analysisRegistry_(analysisRegistry),
      instrumentation_(std::move(instrumentation)), parallelFunctionPasses_(parallelFunctionPasses)
{
}

/// @brief Execute the supplied pipeline against the module.
/// @details Creates an @ref AnalysisManager, materialises each pass via the
///          registry, and invokes it with the module or function as appropriate.
///          After each run the helper invalidates analyses based on the
///          preserved set reported by the pass.  In debug builds it can also run
///          the IL verifier between passes when @p verifyBetweenPasses was set.
/// @param module Module undergoing transformation.
/// @param pipeline Ordered list of pass identifiers.
void PipelineExecutor::run(core::Module &module, const std::vector<std::string> &pipeline) const
{
    AnalysisManager analysis(module, analysisRegistry_);
    const bool collectMetrics = static_cast<bool>(instrumentation_.passMetrics);

    viper::pass::PassManager driver;
    if (instrumentation_.printBefore)
        driver.setPrintBeforeHook(instrumentation_.printBefore);
    if (instrumentation_.printAfter)
        driver.setPrintAfterHook(instrumentation_.printAfter);
    if (instrumentation_.verifyEach)
        driver.setVerifyEachHook(instrumentation_.verifyEach);

    for (const auto &passId : pipeline)
    {
        driver.registerPass(
            passId,
            [this, &module, &analysis, passId, collectMetrics]() -> bool
            {
                PassMetrics metrics{};
                AnalysisCounts countsBefore{};
                std::chrono::steady_clock::time_point startTime{};
                if (collectMetrics)
                {
                    metrics.before = computeIRSize(module);
                    countsBefore = analysis.counts();
                    startTime = std::chrono::steady_clock::now();
                }

                const detail::PassFactory *factory = registry_.lookup(passId);
                if (!factory)
                    return false;

                bool executed = false;
                switch (factory->kind)
                {
                    case detail::PassKind::Module:
                    {
                        if (!factory->makeModule)
                            return false;
                        auto pass = factory->makeModule();
                        if (!pass)
                            return false;
                        PreservedAnalyses preserved = pass->run(module, analysis);
                        analysis.invalidateAfterModulePass(preserved);
                        executed = true;
                        break;
                    }
                    case detail::PassKind::Function:
                    {
                        if (!factory->makeFunction)
                            return false;

                        auto runFunctionPass = [&](core::Function &fn) -> bool
                        {
                            auto pass = factory->makeFunction();
                            if (!pass)
                                return false;
                            PreservedAnalyses preserved = pass->run(fn, analysis);
                            analysis.invalidateAfterFunctionPass(preserved, fn);
                            return true;
                        };

                        bool executedAll = true;
                        if (parallelFunctionPasses_ && module.functions.size() > 1)
                        {
                            const std::size_t workerCount = std::min<std::size_t>(
                                module.functions.size(),
                                std::max<std::size_t>(1, std::thread::hardware_concurrency()));
                            std::atomic_size_t nextIndex{0};
                            std::atomic_bool allOk{true};
                            std::vector<std::thread> workers;
                            workers.reserve(workerCount);
                            for (std::size_t w = 0; w < workerCount; ++w)
                            {
                                workers.emplace_back(
                                    [&]()
                                    {
                                        for (;;)
                                        {
                                            std::size_t idx = nextIndex.fetch_add(1);
                                            if (idx >= module.functions.size())
                                                break;
                                            if (!runFunctionPass(module.functions[idx]))
                                                allOk.store(false, std::memory_order_relaxed);
                                        }
                                    });
                            }
                            for (auto &worker : workers)
                                worker.join();
                            executedAll = allOk.load(std::memory_order_relaxed);
                        }
                        else
                        {
                            for (auto &fn : module.functions)
                                executedAll &= runFunctionPass(fn);
                        }

                        executed = executedAll;
                        break;
                    }
                }

                if (!executed)
                    return false;

                if (collectMetrics && instrumentation_.passMetrics)
                {
                    metrics.after = computeIRSize(module);
                    AnalysisCounts countsAfter = analysis.counts();
                    metrics.analysesComputed.moduleComputations =
                        countsAfter.moduleComputations - countsBefore.moduleComputations;
                    metrics.analysesComputed.functionComputations =
                        countsAfter.functionComputations - countsBefore.functionComputations;
                    metrics.duration = std::chrono::steady_clock::now() - startTime;
                    instrumentation_.passMetrics(passId, metrics);
                }

                return true;
            });
    }

    bool ok = driver.runPipeline(pipeline);
    if (!ok)
    {
#ifndef NDEBUG
        assert(false && "pass pipeline execution failed");
#else
        std::cerr << "warning: pass pipeline execution failed\n";
#endif
    }
}

} // namespace il::transform
