// File: src/il/transform/AnalysisManager.hpp
// Purpose: Declare analysis registration and caching utilities for IL transforms.
// Key invariants: Cached results are invalidated according to preservation summaries.
// Ownership/Lifetime: AnalysisManager borrows module references and owns transient caches per run.
// Links: docs/codemap.md
#pragma once

#include "il/core/fwd.hpp"

#include <any>
#include <cassert>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace il::transform
{

class PreservedAnalyses;
class AnalysisCacheInvalidator;

namespace detail
{
struct ModuleAnalysisRecord
{
    std::function<std::any(core::Module &)> compute;
    std::type_index type{typeid(void)};
};

struct FunctionAnalysisRecord
{
    std::function<std::any(core::Module &, core::Function &)> compute;
    std::type_index type{typeid(void)};
};
} // namespace detail

using ModuleAnalysisMap = std::unordered_map<std::string, detail::ModuleAnalysisRecord>;
using FunctionAnalysisMap = std::unordered_map<std::string, detail::FunctionAnalysisRecord>;

class AnalysisRegistry
{
  public:
    template <typename Result>
    void registerModuleAnalysis(const std::string &id, std::function<Result(core::Module &)> fn)
    {
        moduleAnalyses_[id] = detail::ModuleAnalysisRecord{
            [fn = std::move(fn)](core::Module &module) -> std::any { return fn(module); },
            std::type_index(typeid(Result))};
    }

    template <typename Result>
    void registerFunctionAnalysis(const std::string &id,
                                  std::function<Result(core::Module &, core::Function &)> fn)
    {
        functionAnalyses_[id] = detail::FunctionAnalysisRecord{
            [fn = std::move(fn)](core::Module &module, core::Function &fnRef) -> std::any
            { return fn(module, fnRef); },
            std::type_index(typeid(Result))};
    }

    const ModuleAnalysisMap &moduleAnalyses() const
    {
        return moduleAnalyses_;
    }

    const FunctionAnalysisMap &functionAnalyses() const
    {
        return functionAnalyses_;
    }

  private:
    ModuleAnalysisMap moduleAnalyses_;
    FunctionAnalysisMap functionAnalyses_;
};

class AnalysisManager
{
  public:
    AnalysisManager(core::Module &module, const AnalysisRegistry &registry);

    template <typename Result> Result &getModuleResult(const std::string &id)
    {
        assert(moduleAnalyses_ && "no module analyses registered");
        auto it = moduleAnalyses_->find(id);
        assert(it != moduleAnalyses_->end() && "unknown module analysis");
        std::any &cache = moduleCache_[id];
        if (!cache.has_value())
            cache = it->second.compute(module_);
        assert(it->second.type == std::type_index(typeid(Result)) &&
               "analysis result type mismatch");
        auto *value = std::any_cast<Result>(&cache);
        assert(value && "analysis result cast failed");
        return *value;
    }

    template <typename Result> Result &getFunctionResult(const std::string &id, core::Function &fn)
    {
        assert(functionAnalyses_ && "no function analyses registered");
        auto it = functionAnalyses_->find(id);
        assert(it != functionAnalyses_->end() && "unknown function analysis");
        std::any &cache = functionCache_[id][&fn];
        if (!cache.has_value())
            cache = it->second.compute(module_, fn);
        assert(it->second.type == std::type_index(typeid(Result)) &&
               "analysis result type mismatch");
        auto *value = std::any_cast<Result>(&cache);
        assert(value && "analysis result cast failed");
        return *value;
    }

    void invalidateAfterModulePass(const PreservedAnalyses &preserved);
    void invalidateAfterFunctionPass(const PreservedAnalyses &preserved, core::Function &fn);

    core::Module &module()
    {
        return module_;
    }

    const core::Module &module() const
    {
        return module_;
    }

  private:
    core::Module &module_;
    const ModuleAnalysisMap *moduleAnalyses_ = nullptr;
    const FunctionAnalysisMap *functionAnalyses_ = nullptr;
    std::unordered_map<std::string, std::any> moduleCache_;
    std::unordered_map<std::string, std::unordered_map<const core::Function *, std::any>>
        functionCache_;

    friend class AnalysisCacheInvalidator;
};

} // namespace il::transform
