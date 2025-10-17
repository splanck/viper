//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the modular IL pass manager responsible for orchestrating
// analysis registration, pass pipelines, and verification sequencing.  The
// translation unit focuses on plumbing logic—ensuring each pass executes in the
// correct order while reusing cached analyses—so transformation and analysis
// implementations can remain isolated in their respective modules.
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

/// @brief Construct a pass manager with the default analysis registry entries.
///
/// The constructor enables verification between passes in debug builds and
/// registers the CFG, dominator, and liveness analyses.  Each registration
/// uses a lambda so analysis creation can capture the module/function context
/// provided at execution time.
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

/// @brief Register the SimplifyCFG transformation pass with the pass registry.
///
/// When invoked, the registered callback constructs a SimplifyCFG instance,
/// wires the owning module, and runs the pass on the provided function.  The
/// helper returns a @ref PreservedAnalyses object describing which analysis
/// results remain valid so the pipeline executor can avoid recomputation.
///
/// @param aggressive Whether to enable aggressive simplification heuristics.
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

/// @brief Associate an identifier with a concrete pass pipeline.
///
/// Pipelines are stored by value so callers can compose them once and reuse the
/// same sequence for multiple runs.  Subsequent registrations with the same id
/// replace the existing pipeline.
///
/// @param id       User-visible pipeline identifier.
/// @param pipeline Ordered list of pass invocations to execute when requested.
void PassManager::registerPipeline(const std::string &id, Pipeline pipeline)
{
    pipelines_[id] = std::move(pipeline);
}

/// @brief Retrieve a pipeline previously registered with @ref registerPipeline.
///
/// @param id Identifier supplied during registration.
/// @return Pointer to the stored pipeline or nullptr when @p id is unknown.
const PassManager::Pipeline *PassManager::getPipeline(const std::string &id) const
{
    auto it = pipelines_.find(id);
    return it == pipelines_.end() ? nullptr : &it->second;
}

/// @brief Toggle verification of intermediate IR between consecutive passes.
///
/// The flag is exposed so command-line tools can match the user's debugging
/// preferences.
///
/// @param enable Whether to run the verifier after each pass.
void PassManager::setVerifyBetweenPasses(bool enable)
{
    verifyBetweenPasses_ = enable;
}

/// @brief Execute a concrete pipeline over the supplied module.
///
/// The helper constructs a @ref PipelineExecutor with the registered pass and
/// analysis registries before delegating to its @c run method.
///
/// @param module   Module to transform.
/// @param pipeline Sequence of passes to run.
void PassManager::run(core::Module &module, const Pipeline &pipeline) const
{
    PipelineExecutor executor(passRegistry_, analysisRegistry_, verifyBetweenPasses_);
    executor.run(module, pipeline);
}

/// @brief Execute a named pipeline when present in the registry.
///
/// @param module     Module to transform.
/// @param pipelineId Identifier supplied to @ref registerPipeline.
/// @return True when the pipeline was found and executed; false otherwise.
bool PassManager::runPipeline(core::Module &module, const std::string &pipelineId) const
{
    const Pipeline *pipeline = getPipeline(pipelineId);
    if (!pipeline)
        return false;
    run(module, *pipeline);
    return true;
}

} // namespace il::transform

