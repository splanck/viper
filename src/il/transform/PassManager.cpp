//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the top-level pass manager that collects registered passes,
// constructs pipelines, and dispatches execution through PipelineExecutor.
// This translation unit focuses on the orchestration logic; individual pass
// implementations live elsewhere.
//
//===----------------------------------------------------------------------===//

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

/// @brief Initialise the pass manager with default analyses and settings.
///
/// The constructor enables verification between passes in debug builds and
/// registers a selection of function analyses used by canonical pipelines.
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

/// @brief Register the SimplifyCFG transform in the function pass registry.
///
/// The helper wires up a callback that instantiates @ref SimplifyCFG and
/// advertises preserved analyses when the pass makes no modifications.
///
/// @param aggressive Whether to enable aggressive simplifications.
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

/// @brief Associate a pipeline identifier with a sequence of pass identifiers.
///
/// Pipelines are stored by value so later calls to @ref runPipeline can look up
/// the configured sequence.
///
/// @param id Stable identifier used by tools or clients to request execution.
/// @param pipeline Ordered list of pass identifiers.
void PassManager::registerPipeline(const std::string &id, Pipeline pipeline)
{
    pipelines_[id] = std::move(pipeline);
}

/// @brief Look up a previously registered pipeline definition.
///
/// @param id Identifier used during registration.
/// @return Pointer to the pipeline or @c nullptr when the identifier is unknown.
const PassManager::Pipeline *PassManager::getPipeline(const std::string &id) const
{
    auto it = pipelines_.find(id);
    return it == pipelines_.end() ? nullptr : &it->second;
}

/// @brief Enable or disable verifier checks between pipeline passes.
///
/// @param enable When @c true, the executor will run the IL verifier after each
///               pass in debug builds.
void PassManager::setVerifyBetweenPasses(bool enable)
{
    verifyBetweenPasses_ = enable;
}

/// @brief Execute a specific pipeline against a module.
///
/// Constructs a @ref PipelineExecutor with the currently registered passes and
/// analyses, then forwards the module to it for execution.
///
/// @param module Module undergoing transformation.
/// @param pipeline Ordered list of pass identifiers to execute.
void PassManager::run(core::Module &module, const Pipeline &pipeline) const
{
    PipelineExecutor executor(passRegistry_, analysisRegistry_, verifyBetweenPasses_);
    executor.run(module, pipeline);
}

/// @brief Execute a named pipeline if it exists.
///
/// @param module Module undergoing transformation.
/// @param pipelineId Identifier of the desired pipeline.
/// @return @c true when the pipeline was found and executed; otherwise @c false.
bool PassManager::runPipeline(core::Module &module, const std::string &pipelineId) const
{
    const Pipeline *pipeline = getPipeline(pipelineId);
    if (!pipeline)
        return false;
    run(module, *pipeline);
    return true;
}

} // namespace il::transform

