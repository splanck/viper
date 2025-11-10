//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

#include <cassert>
#include <utility>

namespace il::transform
{

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
                                   Instrumentation instrumentation)
    : registry_(registry), analysisRegistry_(analysisRegistry),
      instrumentation_(std::move(instrumentation))
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

    viper::pass::PassManager driver;
    if (instrumentation_.printBefore)
        driver.setPrintBeforeHook(instrumentation_.printBefore);
    if (instrumentation_.printAfter)
        driver.setPrintAfterHook(instrumentation_.printAfter);
    if (instrumentation_.verifyEach)
        driver.setVerifyEachHook(instrumentation_.verifyEach);

    for (const auto &passId : pipeline)
    {
        driver.registerPass(passId,
                            [this, &module, &analysis, passId]() -> bool
                            {
                                const detail::PassFactory *factory = registry_.lookup(passId);
                                if (!factory)
                                    return false;

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
                                        return true;
                                    }
                                    case detail::PassKind::Function:
                                    {
                                        if (!factory->makeFunction)
                                            return false;
                                        for (auto &fn : module.functions)
                                        {
                                            auto pass = factory->makeFunction();
                                            if (!pass)
                                                continue;
                                            PreservedAnalyses preserved = pass->run(fn, analysis);
                                            analysis.invalidateAfterFunctionPass(preserved, fn);
                                        }
                                        return true;
                                    }
                                }

                                return false;
                            });
    }

    bool ok = driver.runPipeline(pipeline);
#ifndef NDEBUG
    assert(ok && "pass pipeline execution failed");
#else
    (void)ok;
#endif
}

} // namespace il::transform
