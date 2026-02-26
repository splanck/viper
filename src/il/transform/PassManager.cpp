//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/PassManager.cpp
// Purpose: Implement the orchestration layer that registers passes, manages
//          pipelines, and invokes the executor to run them.
// Links: docs/architecture.md#passes
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines the pass manager responsible for constructing and executing transformation
/// pipelines.
/// @details Provides registration facilities for analyses, passes, and pipelines
///          while delegating execution to @ref PipelineExecutor.  Keeping this
///          coordination logic isolated simplifies testing and maintenance of
///          the transformation stack.

#include "il/transform/PassManager.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/analysis/MemorySSA.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/AnalysisIDs.hpp"
#include "il/transform/CheckOpt.hpp"
#include "il/transform/LICM.hpp"
#include "il/transform/LateCleanup.hpp"
#include "il/transform/LoopUnroll.hpp"
#include "il/transform/PipelineExecutor.hpp"
#include "il/transform/SiblingRecursion.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "viper/pass/PassManager.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <utility>

using namespace il::core;

namespace il::transform
{

/// @brief Initialise the pass manager with default analyses and settings.
/// @details Enables verification between passes in debug builds and registers
///          core analyses (CFG, dominator tree, and liveness) used by canonical
///          pipelines.  The registrations install factory callbacks that lazily
///          compute results when passes request them.
PassManager::PassManager()
{
#ifndef NDEBUG
    verifyBetweenPasses_ = true;
#else
    verifyBetweenPasses_ = false;
#endif
    instrumentationStream_ = &std::cerr;

    analysisRegistry_.registerFunctionAnalysis<CFGInfo>(kAnalysisCFG,
                                                        [](core::Module &module, core::Function &fn)
                                                        { return buildCFG(module, fn); });
    analysisRegistry_.registerFunctionAnalysis<viper::analysis::DomTree>(
        kAnalysisDominators,
        [](core::Module &module, core::Function &fn)
        {
            viper::analysis::CFGContext ctx(module);
            return viper::analysis::computeDominatorTree(ctx, fn);
        });
    analysisRegistry_.registerFunctionAnalysis<LoopInfo>(
        kAnalysisLoopInfo,
        [](core::Module &module, core::Function &fn) { return computeLoopInfo(module, fn); });
    analysisRegistry_.registerFunctionAnalysis<LivenessInfo>(
        kAnalysisLiveness,
        [](core::Module &module, core::Function &fn) { return computeLiveness(module, fn); });
    // Basic alias analysis for memory disambiguation (available to DSE/LICM etc.)
    analysisRegistry_.registerFunctionAnalysis<viper::analysis::BasicAA>(
        kAnalysisBasicAA,
        [](core::Module &module, core::Function &fn)
        { return viper::analysis::BasicAA(module, fn); });
    // MemorySSA: precise def-use chains for memory operations; used by DSE for
    // cross-block dead-store elimination without false read-barriers on calls.
    analysisRegistry_.registerFunctionAnalysis<viper::analysis::MemorySSA>(
        kAnalysisMemorySSA,
        [](core::Module &module, core::Function &fn)
        {
            viper::analysis::BasicAA aa(module, fn);
            return viper::analysis::computeMemorySSA(fn, aa);
        });

    addSimplifyCFG(false); // Register simplify-cfg pass (non-aggressive by default)
    registerLoopSimplifyPass(passRegistry_);
    registerLICMPass(passRegistry_);
    registerSCCPPass(passRegistry_);
    registerConstFoldPass(passRegistry_);
    registerPeepholePass(passRegistry_);
    registerDCEPass(passRegistry_);
    registerMem2RegPass(passRegistry_);
    registerDSEPass(passRegistry_);
    registerEarlyCSEPass(passRegistry_);
    registerGVNPass(passRegistry_);
    registerIndVarSimplifyPass(passRegistry_);
    registerInlinePass(passRegistry_);
    registerCheckOptPass(passRegistry_);
    registerLateCleanupPass(passRegistry_);
    registerLoopUnrollPass(passRegistry_);
    registerSiblingRecursionPass(passRegistry_);

    // Pre-register common pipelines
    registerPipeline("O0", {"simplify-cfg", "dce"});
    registerPipeline("O1",
                     {"simplify-cfg",
                      "mem2reg",
                      "simplify-cfg",
                      "sccp",
                      "dce",
                      "simplify-cfg",
                      "licm",
                      "simplify-cfg",
                      "peephole",
                      "dce"});
    // O2 pipeline with interprocedural constant propagation:
    // Run SCCP both before (to simplify callees) and after inline
    // (to propagate constants through inlined code from call sites).
    registerPipeline("O2", {"loop-simplify", "indvars",      "loop-unroll",  "simplify-cfg",
                            "mem2reg",       "simplify-cfg",
                            "sccp", // Pre-inline SCCP: simplify callees
                            "check-opt",     "dce",          "simplify-cfg", "sibling-recursion",
                            "inline",        "simplify-cfg",
                            "sccp", // Post-inline SCCP: propagate call-site constants
                            "dce",  // Clean up after second SCCP
                            "simplify-cfg",  "licm",         "simplify-cfg", "gvn",
                            "earlycse",      "dse",          "peephole",     "dce",
                            "late-cleanup"});
}

/// @brief Register the SimplifyCFG transform in the function pass registry.
/// @details Installs a factory that constructs @ref SimplifyCFG with the
///          requested aggressiveness.  When the pass reports no changes the
///          helper returns a fully preserved analysis set; otherwise analyses are
///          invalidated so downstream passes recompute what they need.
/// @param aggressive Whether to enable aggressive simplifications.
void PassManager::addSimplifyCFG(bool aggressive)
{
    passRegistry_.registerFunctionPass(
        "simplify-cfg",
        [aggressive](core::Function &function, AnalysisManager &analysis)
        {
            SimplifyCFG pass(aggressive);
            pass.setModule(&analysis.module());
            pass.setAnalysisManager(&analysis);
            bool changed = pass.run(function, nullptr);
            if (!changed)
                return PreservedAnalyses::all();

            PreservedAnalyses preserved;
            preserved.preserveAllModules();
            return preserved;
        });
}

/// @brief Associate a pipeline identifier with a sequence of pass identifiers.
/// @details Stores the pipeline in an internal map keyed by @p id so later calls
///          can retrieve it.  Pipelines are copied into the map to keep the API
///          independent of the caller's container lifetimes.
/// @param id Stable identifier used by tools or clients to request execution.
/// @param pipeline Ordered list of pass identifiers.
void PassManager::registerPipeline(const std::string &id, Pipeline pipeline)
{
    pipelines_[id] = std::move(pipeline);
}

/// @brief Look up a previously registered pipeline definition.
/// @details Performs a map lookup and returns a pointer to the stored pipeline
///          when found.  Unknown identifiers yield @c nullptr so callers can
///          report missing configurations gracefully.
/// @param id Identifier used during registration.
/// @return Pointer to the pipeline or @c nullptr when the identifier is unknown.
const PassManager::Pipeline *PassManager::getPipeline(const std::string &id) const
{
    auto it = pipelines_.find(id);
    return it == pipelines_.end() ? nullptr : &it->second;
}

/// @brief Enable or disable verifier checks between pipeline passes.
/// @details Toggles the flag forwarded to @ref PipelineExecutor so debug builds
///          can optionally verify module integrity between passes.
/// @param enable When @c true, the executor will run the IL verifier after each
///               pass in debug builds.
void PassManager::setVerifyBetweenPasses(bool enable)
{
    verifyBetweenPasses_ = enable;
}

void PassManager::setPrintBeforeEach(bool enable)
{
    printBeforeEach_ = enable;
}

void PassManager::setPrintAfterEach(bool enable)
{
    printAfterEach_ = enable;
}

void PassManager::setInstrumentationStream(std::ostream &os)
{
    instrumentationStream_ = &os;
}

void PassManager::setReportPassStatistics(bool enable)
{
    reportPassStatistics_ = enable;
}

void PassManager::enableParallelFunctionPasses(bool enable)
{
    parallelFunctionPasses_ = enable;
}

/// @brief Execute a specific pipeline against a module.
/// @details Constructs a @ref PipelineExecutor using the current pass and
///          analysis registries, then invokes it with the provided pipeline.
///          Ownership of passes remains with the executor, keeping the manager
///          itself stateless.
/// @param module Module undergoing transformation.
/// @param pipeline Ordered list of pass identifiers to execute.
void PassManager::run(core::Module &module, const Pipeline &pipeline) const
{
    PipelineExecutor::Instrumentation instrumentation{};

    if (printBeforeEach_ && instrumentationStream_)
    {
        instrumentation.printBefore = [this, &module](std::string_view passId)
        {
            *instrumentationStream_ << "*** IR before pass '" << passId << "' ***\n";
            il::io::Serializer::write(module, *instrumentationStream_);
            *instrumentationStream_ << "\n";
        };
    }

    if (printAfterEach_ && instrumentationStream_)
    {
        instrumentation.printAfter = [this, &module](std::string_view passId)
        {
            *instrumentationStream_ << "*** IR after pass '" << passId << "' ***\n";
            il::io::Serializer::write(module, *instrumentationStream_);
            *instrumentationStream_ << "\n";
        };
    }

    if (verifyBetweenPasses_)
    {
        instrumentation.verifyEach = [this, &module](std::string_view passId)
        {
            auto result = il::verify::Verifier::verify(module);
            if (!result)
            {
                if (instrumentationStream_)
                {
                    *instrumentationStream_ << "verification failed after pass '" << passId
                                            << "'\n";
                    il::support::printDiag(result.error(), *instrumentationStream_);
                    *instrumentationStream_ << "\n";
                }
#ifndef NDEBUG
                assert(false && "IL verification failed after pass");
#endif
                return false;
            }
            return true;
        };
    }

    if (reportPassStatistics_ && instrumentationStream_)
    {
        instrumentation.passMetrics =
            [this](std::string_view passId, const PipelineExecutor::PassMetrics &metrics)
        {
            if (!instrumentationStream_)
                return;
            const auto micros =
                std::chrono::duration_cast<std::chrono::microseconds>(metrics.duration).count();
            *instrumentationStream_
                << "[pass " << passId << "] bb " << metrics.before.blocks << " -> "
                << metrics.after.blocks << ", inst " << metrics.before.instructions << " -> "
                << metrics.after.instructions
                << ", analyses M:" << metrics.analysesComputed.moduleComputations
                << " F:" << metrics.analysesComputed.functionComputations << ", time " << micros
                << "us\n";
        };
    }

    PipelineExecutor executor(
        passRegistry_, analysisRegistry_, std::move(instrumentation), parallelFunctionPasses_);
    executor.run(module, pipeline);
}

/// @brief Execute a named pipeline if it exists.
/// @details Uses @ref getPipeline to retrieve the configuration and delegates to
///          @ref run when found.  Returns false when the pipeline identifier is
///          unknown so callers can fall back to alternative behaviours.
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
