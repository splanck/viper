// File: src/il/transform/PipelineExecutor.cpp
// License: MIT License. See LICENSE in the project root for details.
// Purpose: Implement the helper that executes pass pipelines and manages verification.
// Key invariants: Analysis caches are invalidated according to preservation summaries between passes.
// Ownership/Lifetime: Executor borrows registries and modules owned by callers; no persistent state.
// Links: docs/codemap.md

#include "il/transform/PipelineExecutor.hpp"

#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>

namespace il::transform
{

PipelineExecutor::PipelineExecutor(const PassRegistry &registry,
                                   const AnalysisRegistry &analysisRegistry,
                                   bool verifyBetweenPasses)
    : registry_(registry), analysisRegistry_(analysisRegistry), verifyBetweenPasses_(verifyBetweenPasses)
{
}

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

