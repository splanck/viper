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
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::transform
{

struct SimplifyCFG;

class PipelineExecutor;

class PassManager
{
  public:
    using Pipeline = std::vector<std::string>;

    PassManager();

    PassRegistry &passes()
    {
        return passRegistry_;
    }

    const PassRegistry &passes() const
    {
        return passRegistry_;
    }

    AnalysisRegistry &analyses()
    {
        return analysisRegistry_;
    }

    const AnalysisRegistry &analyses() const
    {
        return analysisRegistry_;
    }

    template <typename Result>
    void registerModuleAnalysis(const std::string &id, std::function<Result(core::Module &)> fn)
    {
        analysisRegistry_.registerModuleAnalysis<Result>(id, std::move(fn));
    }

    template <typename Result>
    void registerFunctionAnalysis(const std::string &id,
                                  std::function<Result(core::Module &, core::Function &)> fn)
    {
        analysisRegistry_.registerFunctionAnalysis<Result>(id, std::move(fn));
    }

    void registerModulePass(const std::string &id, PassRegistry::ModulePassFactory factory)
    {
        passRegistry_.registerModulePass(id, std::move(factory));
    }

    void registerModulePass(const std::string &id, PassRegistry::ModulePassCallback callback)
    {
        passRegistry_.registerModulePass(id, std::move(callback));
    }

    void registerModulePass(const std::string &id, const std::function<void(core::Module &)> &fn)
    {
        passRegistry_.registerModulePass(id, fn);
    }

    void registerFunctionPass(const std::string &id, PassRegistry::FunctionPassFactory factory)
    {
        passRegistry_.registerFunctionPass(id, std::move(factory));
    }

    void registerFunctionPass(const std::string &id, PassRegistry::FunctionPassCallback callback)
    {
        passRegistry_.registerFunctionPass(id, std::move(callback));
    }

    void registerFunctionPass(const std::string &id,
                              const std::function<void(core::Function &)> &fn)
    {
        passRegistry_.registerFunctionPass(id, fn);
    }

    void addSimplifyCFG(bool aggressive = true);

    void registerPipeline(const std::string &id, Pipeline pipeline);
    const Pipeline *getPipeline(const std::string &id) const;

    void setVerifyBetweenPasses(bool enable);

    void run(core::Module &module, const Pipeline &pipeline) const;
    bool runPipeline(core::Module &module, const std::string &pipelineId) const;

  private:
    AnalysisRegistry analysisRegistry_;
    PassRegistry passRegistry_;
    std::unordered_map<std::string, Pipeline> pipelines_;
    bool verifyBetweenPasses_ = false;
};

} // namespace il::transform
