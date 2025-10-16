// File: src/il/transform/PassManager.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implement the modular IL pass manager focused on pipeline orchestration.
// Key invariants: Pipelines execute in registration order and reuse shared helper infrastructure.
// Ownership/Lifetime: PassManager owns registries and delegates execution to helper classes.
// Links: docs/codemap.md

#include "il/transform/PassManager.hpp"

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/transform/PipelineExecutor.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/transform/analysis/Liveness.hpp"

#include <utility>

using namespace il::core;

namespace il::transform
{

PassManager::PassManager()
{
#ifndef NDEBUG
    verifyBetweenPasses_ = true;
#else
    verifyBetweenPasses_ = false;
#endif

    analysisRegistry_.registerFunctionAnalysis<CFGInfo>(
        "cfg", [](core::Module &module, core::Function &fn) { return buildCFG(module, fn); });
    analysisRegistry_.registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](core::Module &module, core::Function &fn)
        {
            viper::analysis::CFGContext ctx(module);
            return viper::analysis::computeDominatorTree(ctx, fn);
        });
    analysisRegistry_.registerFunctionAnalysis<LivenessInfo>(
        "liveness",
        [](core::Module &module, core::Function &fn) { return computeLiveness(module, fn); });
}

void PassManager::addSimplifyCFG(bool aggressive)
{
    passRegistry_.registerFunctionPass(
        "simplify-cfg",
        [aggressive](core::Function &function, AnalysisManager &analysis)
        {
            SimplifyCFG pass(aggressive);
            pass.setModule(&analysis.module());
            bool changed = pass.run(function, nullptr);
            if (!changed)
                return PreservedAnalyses::all();

            PreservedAnalyses preserved;
            preserved.preserveAllModules();
            return preserved;
        });
}

void PassManager::registerPipeline(const std::string &id, Pipeline pipeline)
{
    pipelines_[id] = std::move(pipeline);
}

const PassManager::Pipeline *PassManager::getPipeline(const std::string &id) const
{
    auto it = pipelines_.find(id);
    return it == pipelines_.end() ? nullptr : &it->second;
}

void PassManager::setVerifyBetweenPasses(bool enable)
{
    verifyBetweenPasses_ = enable;
}

void PassManager::run(core::Module &module, const Pipeline &pipeline) const
{
    PipelineExecutor executor(passRegistry_, analysisRegistry_, verifyBetweenPasses_);
    executor.run(module, pipeline);
}

bool PassManager::runPipeline(core::Module &module, const std::string &pipelineId) const
{
    const Pipeline *pipeline = getPipeline(pipelineId);
    if (!pipeline)
        return false;
    run(module, *pipeline);
    return true;
}

} // namespace il::transform

