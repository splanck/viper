//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the helper responsible for iterating over pass pipelines, invoking
// registered factories, and invalidating cached analyses after each run.  The
// executor is intentionally stateless so pass managers can construct temporary
// instances per invocation.
//
//===----------------------------------------------------------------------===//

#include "il/transform/PipelineExecutor.hpp"

#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>

namespace il::transform
{

/// @brief Construct an executor bound to specific pass and analysis registries.
///
/// @param registry Pass registry supplying factories for module/function passes.
/// @param analysisRegistry Registry describing available analyses.
/// @param verifyBetweenPasses Controls whether debug builds run the verifier
///                            after each pass.
PipelineExecutor::PipelineExecutor(const PassRegistry &registry,
                                   const AnalysisRegistry &analysisRegistry,
                                   bool verifyBetweenPasses)
    : registry_(registry), analysisRegistry_(analysisRegistry), verifyBetweenPasses_(verifyBetweenPasses)
{
}

/// @brief Execute the supplied pipeline against the module.
///
/// The executor creates a fresh @ref AnalysisManager for the module, then walks
/// the pipeline identifiers.  For each entry it materialises the requested pass
/// via the registry, runs it, and invalidates analyses based on the preservation
/// summary returned by the pass.  Optional verification occurs between passes in
/// debug builds.
///
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

