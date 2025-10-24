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
#include "il/verify/Verifier.hpp"

#include <cassert>

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
                                   bool verifyBetweenPasses)
    : registry_(registry), analysisRegistry_(analysisRegistry),
      verifyBetweenPasses_(verifyBetweenPasses)
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

    for (const auto &passId : pipeline)
    {
        const detail::PassFactory *factory = registry_.lookup(passId);
        if (!factory)
            continue;

        switch (factory->kind)
        {
            case detail::PassKind::Module:
            {
                if (!factory->makeModule)
                    break;
                auto pass = factory->makeModule();
                if (!pass)
                    break;
                PreservedAnalyses preserved = pass->run(module, analysis);
                analysis.invalidateAfterModulePass(preserved);
                break;
            }
            case detail::PassKind::Function:
            {
                if (!factory->makeFunction)
                    break;
                for (auto &fn : module.functions)
                {
                    auto pass = factory->makeFunction();
                    if (!pass)
                        continue;
                    PreservedAnalyses preserved = pass->run(fn, analysis);
                    analysis.invalidateAfterFunctionPass(preserved, fn);
                }
                break;
            }
        }

#ifndef NDEBUG
        if (verifyBetweenPasses_)
        {
            auto verified = il::verify::Verifier::verify(module);
            assert(verified && "IL verification failed after pass");
        }
#endif
    }
}

} // namespace il::transform
