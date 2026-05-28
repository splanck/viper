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
#include <string_view>
#include <thread>
#include <utility>

namespace il::transform {
namespace {
PipelineExecutor::PassMetrics::IRSize computeIRSize(const core::Module &module) {
    PipelineExecutor::PassMetrics::IRSize size{};
    for (const auto &fn : module.functions) {
        size.blocks += fn.blocks.size();
        for (const auto &block : fn.blocks)
            size.instructions += block.instructions.size();
    }
    return size;
}

bool isCleanupPass(std::string_view passId) {
    return passId == "dce" || passId == "simplify-cfg" || passId == "late-cleanup";
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
      instrumentation_(std::move(instrumentation)),
      parallelFunctionPasses_(parallelFunctionPasses) {}

/// @brief Execute the supplied pipeline against the module.
/// @details Creates an @ref AnalysisManager, materialises each pass via the
///          registry, and invokes it with the module or function as appropriate.
///          After each run the helper invalidates analyses based on the
///          preserved set reported by the pass.  In debug builds it can also run
///          the IL verifier between passes when @p verifyBetweenPasses was set.
/// @param module Module undergoing transformation.
/// @param pipeline Ordered list of pass identifiers.
bool PipelineExecutor::run(core::Module &module, const std::vector<std::string> &pipeline) const {
    AnalysisManager analysis(module, analysisRegistry_);
    const bool collectMetrics = static_cast<bool>(instrumentation_.passMetrics);

    viper::pass::PassManager driver;
    if (instrumentation_.printBefore)
        driver.setPrintBeforeHook(instrumentation_.printBefore);
    if (instrumentation_.printAfter)
        driver.setPrintAfterHook(instrumentation_.printAfter);

    bool changedSinceLastCleanup = true;
    bool hasRunAnyPass = false;
    for (const auto &passId : pipeline) {
        driver.registerPass(
            passId,
            [this,
             &module,
             &analysis,
             &changedSinceLastCleanup,
             &hasRunAnyPass,
             passId,
             collectMetrics]() -> bool {
                if (hasRunAnyPass && isCleanupPass(passId) && !changedSinceLastCleanup)
                    return true;

                PassMetrics metrics{};
                AnalysisCounts countsBefore{};
                std::chrono::steady_clock::time_point startTime{};
                std::chrono::steady_clock::time_point passEndTime{};
                if (collectMetrics) {
                    metrics.before = computeIRSize(module);
                    countsBefore = analysis.counts();
                    startTime = std::chrono::steady_clock::now();
                }

                const detail::PassFactory *factory = registry_.lookup(passId);
                if (!factory)
                    return false;

                bool executed = false;
                bool passChanged = false;
                switch (factory->kind) {
                    case detail::PassKind::Module: {
                        if (!factory->makeModule)
                            return false;
                        auto pass = factory->makeModule();
                        if (!pass)
                            return false;
                        PreservedAnalyses preserved = pass->run(module, analysis);
                        passChanged = !preserved.preservesAllAnalyses();
                        analysis.invalidateAfterModulePass(preserved);
                        executed = true;
                        break;
                    }
                    case detail::PassKind::Function: {
                        if (!factory->makeFunction)
                            return false;

                        auto runFunctionPass = [&](core::Function &fn,
                                                   AnalysisManager &functionAnalysis,
                                                   bool &functionChanged) -> bool {
                            auto pass = factory->makeFunction();
                            if (!pass)
                                return false;
                            PreservedAnalyses preserved = pass->run(fn, functionAnalysis);
                            functionChanged = !preserved.preservesAllAnalyses();
                            functionAnalysis.invalidateAfterFunctionPass(preserved, fn);
                            return true;
                        };

                        bool executedAll = true;
                        // Parallel execution is reserved for function passes
                        // that were explicitly audited and registered as safe.
                        if (parallelFunctionPasses_ && factory->parallelSafe &&
                            module.functions.size() > 1) {
                            const std::size_t workerCount = std::min<std::size_t>(
                                module.functions.size(),
                                std::max<std::size_t>(1, std::thread::hardware_concurrency()));
                            std::atomic_size_t nextIndex{0};
                            std::atomic_bool allOk{true};
                            std::atomic_bool anyChanged{false};
                            std::vector<std::thread> workers;
                            workers.reserve(workerCount);
                            for (std::size_t w = 0; w < workerCount; ++w) {
                                workers.emplace_back([&]() {
                                    AnalysisManager workerAnalysis(module, analysisRegistry_);
                                    for (;;) {
                                        std::size_t idx = nextIndex.fetch_add(1);
                                        if (idx >= module.functions.size())
                                            break;
                                        bool functionChanged = false;
                                        if (!runFunctionPass(module.functions[idx],
                                                             workerAnalysis,
                                                             functionChanged))
                                            allOk.store(false, std::memory_order_relaxed);
                                        if (functionChanged)
                                            anyChanged.store(true, std::memory_order_relaxed);
                                    }
                                });
                            }
                            for (auto &worker : workers)
                                worker.join();
                            executedAll = allOk.load(std::memory_order_relaxed);
                            passChanged = anyChanged.load(std::memory_order_relaxed);
                        } else {
                            for (auto &fn : module.functions) {
                                bool functionChanged = false;
                                executedAll &= runFunctionPass(fn, analysis, functionChanged);
                                passChanged = passChanged || functionChanged;
                            }
                        }

                        executed = executedAll;
                        break;
                    }
                }

                if (!executed)
                    return false;

                if (collectMetrics)
                    passEndTime = std::chrono::steady_clock::now();

                if (instrumentation_.verifyEach) {
                    const auto verifyStart = std::chrono::steady_clock::now();
                    const bool verified = instrumentation_.verifyEach(passId);
                    metrics.verifyRan = true;
                    if (collectMetrics)
                        metrics.verifyDuration = std::chrono::steady_clock::now() - verifyStart;
                    if (!verified)
                        return false;
                }

                if (collectMetrics && instrumentation_.passMetrics) {
                    metrics.after = computeIRSize(module);
                    AnalysisCounts countsAfter = analysis.counts();
                    metrics.analysesComputed.moduleComputations =
                        countsAfter.moduleComputations - countsBefore.moduleComputations;
                    metrics.analysesComputed.functionComputations =
                        countsAfter.functionComputations - countsBefore.functionComputations;
                    metrics.duration = passEndTime - startTime;
                    instrumentation_.passMetrics(passId, metrics);
                }

                hasRunAnyPass = true;
                if (isCleanupPass(passId))
                    changedSinceLastCleanup = passChanged;
                else if (passChanged)
                    changedSinceLastCleanup = true;

                return true;
            });
    }

    bool ok = driver.runPipeline(pipeline);
    if (!ok) {
        std::cerr << "warning: pass pipeline execution failed\n";
    }
    return ok;
}

} // namespace il::transform
