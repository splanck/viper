// File: src/il/transform/PassManager.hpp
// Purpose: Declare the IL pass manager responsible for orchestrating pipelines.
// Key invariants: Pipelines execute registered passes in order with consistent verification
// semantics. Ownership/Lifetime: PassManager owns pass/analysis registries and borrows modules
// during execution. Links: docs/codemap.md
#pragma once

#include "il/core/fwd.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/PassRegistry.hpp"

#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::transform
{

struct SimplifyCFG;

class PipelineExecutor;

/// @brief Orchestrates execution of transformation passes and analyses on IL modules.
/// @details The PassManager maintains registries for passes and analyses,
///          builds optimization pipelines, and provides debugging features like
///          verification and printing between pass executions.
class PassManager
{
  public:
    using Pipeline = std::vector<std::string>;

    /// @brief Construct a new PassManager with default settings.
    PassManager();

    /// @brief Get mutable access to the pass registry.
    /// @return Reference to the internal PassRegistry.
    PassRegistry &passes()
    {
        return passRegistry_;
    }

    /// @brief Get const access to the pass registry.
    /// @return Const reference to the internal PassRegistry.
    const PassRegistry &passes() const
    {
        return passRegistry_;
    }

    /// @brief Get mutable access to the analysis registry.
    /// @return Reference to the internal AnalysisRegistry.
    AnalysisRegistry &analyses()
    {
        return analysisRegistry_;
    }

    /// @brief Get const access to the analysis registry.
    /// @return Const reference to the internal AnalysisRegistry.
    const AnalysisRegistry &analyses() const
    {
        return analysisRegistry_;
    }

    /// @brief Register a module-level analysis.
    /// @tparam Result Type returned by the analysis computation.
    /// @param id Unique identifier for the analysis.
    /// @param fn Function computing the analysis result from a module.
    template <typename Result>
    void registerModuleAnalysis(const std::string &id, std::function<Result(core::Module &)> fn)
    {
        analysisRegistry_.registerModuleAnalysis<Result>(id, std::move(fn));
    }

    /// @brief Register a function-level analysis.
    /// @tparam Result Type returned by the analysis computation.
    /// @param id Unique identifier for the analysis.
    /// @param fn Function computing the analysis result from a function.
    template <typename Result>
    void registerFunctionAnalysis(const std::string &id,
                                  std::function<Result(core::Module &, core::Function &)> fn)
    {
        analysisRegistry_.registerFunctionAnalysis<Result>(id, std::move(fn));
    }

    /// @brief Register a module pass using a factory function.
    /// @param id Unique identifier for the pass.
    /// @param factory Function returning a new ModulePass instance.
    void registerModulePass(const std::string &id, PassRegistry::ModulePassFactory factory)
    {
        passRegistry_.registerModulePass(id, std::move(factory));
    }

    /// @brief Register a module pass using a callback with analysis access.
    /// @param id Unique identifier for the pass.
    /// @param callback Function implementing the pass transformation.
    void registerModulePass(const std::string &id, PassRegistry::ModulePassCallback callback)
    {
        passRegistry_.registerModulePass(id, std::move(callback));
    }

    /// @brief Register a simple module pass without analysis access.
    /// @param id Unique identifier for the pass.
    /// @param fn Function transforming the module (no return value).
    void registerModulePass(const std::string &id, const std::function<void(core::Module &)> &fn)
    {
        passRegistry_.registerModulePass(id, fn);
    }

    /// @brief Register a function pass using a factory function.
    /// @param id Unique identifier for the pass.
    /// @param factory Function returning a new FunctionPass instance.
    void registerFunctionPass(const std::string &id, PassRegistry::FunctionPassFactory factory)
    {
        passRegistry_.registerFunctionPass(id, std::move(factory));
    }

    /// @brief Register a function pass using a callback with analysis access.
    /// @param id Unique identifier for the pass.
    /// @param callback Function implementing the pass transformation.
    void registerFunctionPass(const std::string &id, PassRegistry::FunctionPassCallback callback)
    {
        passRegistry_.registerFunctionPass(id, std::move(callback));
    }

    /// @brief Register a simple function pass without analysis access.
    /// @param id Unique identifier for the pass.
    /// @param fn Function transforming a function (no return value).
    void registerFunctionPass(const std::string &id,
                              const std::function<void(core::Function &)> &fn)
    {
        passRegistry_.registerFunctionPass(id, fn);
    }

    /// @brief Add the SimplifyCFG pass to the pass registry.
    /// @param aggressive Enable aggressive control flow simplification.
    void addSimplifyCFG(bool aggressive = true);

    /// @brief Register a named pipeline of pass identifiers.
    /// @param id Unique identifier for the pipeline.
    /// @param pipeline Ordered list of pass names to execute.
    void registerPipeline(const std::string &id, Pipeline pipeline);

    /// @brief Look up a registered pipeline by name.
    /// @param id Pipeline identifier to query.
    /// @return Pointer to pipeline if found, nullptr otherwise.
    const Pipeline *getPipeline(const std::string &id) const;

    /// @brief Enable or disable module verification between passes.
    /// @param enable True to verify IR consistency after each pass.
    void setVerifyBetweenPasses(bool enable);

    /// @brief Enable or disable printing IR before each pass.
    /// @param enable True to print the module before pass execution.
    void setPrintBeforeEach(bool enable);

    /// @brief Enable or disable printing IR after each pass.
    /// @param enable True to print the module after pass execution.
    void setPrintAfterEach(bool enable);

    /// @brief Set the output stream for instrumentation output.
    /// @param os Stream for printing IR and diagnostics.
    void setInstrumentationStream(std::ostream &os);

    /// @brief Execute a pipeline of passes on a module.
    /// @param module Module to transform.
    /// @param pipeline Ordered list of pass identifiers to run.
    void run(core::Module &module, const Pipeline &pipeline) const;

    /// @brief Execute a named registered pipeline on a module.
    /// @param module Module to transform.
    /// @param pipelineId Identifier of the registered pipeline.
    /// @return True if the pipeline was found and executed.
    bool runPipeline(core::Module &module, const std::string &pipelineId) const;

  private:
    AnalysisRegistry analysisRegistry_;
    PassRegistry passRegistry_;
    std::unordered_map<std::string, Pipeline> pipelines_;
    bool verifyBetweenPasses_ = false;
    bool printBeforeEach_ = false;
    bool printAfterEach_ = false;
    std::ostream *instrumentationStream_ = nullptr;
};

} // namespace il::transform
