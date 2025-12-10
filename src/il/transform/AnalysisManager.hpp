//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the analysis manager, which handles registration, caching,
// and invalidation of analysis results during pass pipeline execution. Analyses
// compute properties of IL modules (CFG, dominators, liveness) that multiple
// passes can reuse without redundant computation.
//
// Optimization passes often depend on common analyses like control flow graphs
// or dominator trees. Computing these analyses is expensive; recomputing them
// after each pass would be wasteful. The analysis manager caches analysis results
// and tracks which passes invalidate which analyses, enabling efficient reuse
// of expensive computations.
//
// Caching and Invalidation Model:
// - Registration: Each analysis registers a compute function that produces results
//   from a module or function
// - On-demand computation: When a pass requests an analysis, the manager checks
//   the cache. If results exist and are valid, they're returned. Otherwise, the
//   analysis is computed and cached.
// - Preservation-based invalidation: After each pass, the manager consults the
//   pass's PreservedAnalyses metadata. Only analyses not marked as preserved are
//   invalidated and removed from the cache.
//
// This design enables optimal performance while maintaining correctness: analyses
// are computed exactly once until a transformation invalidates them.
//
//===----------------------------------------------------------------------===//
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

struct AnalysisCounts
{
    std::size_t moduleComputations = 0;
    std::size_t functionComputations = 0;
};

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

/// @brief Manages computation and caching of analysis results during pass execution.
/// @details The AnalysisManager lazily computes analyses on demand, caches results,
///          and invalidates stale caches based on PreservedAnalyses information
///          from passes. Module and function analyses are tracked separately.
class AnalysisManager
{
  public:
    /// @brief Construct an AnalysisManager for a module.
    /// @param module Module this manager operates on.
    /// @param registry Registry containing registered analysis factories.
    AnalysisManager(core::Module &module, const AnalysisRegistry &registry);

    /// @brief Retrieve or compute a module-level analysis result.
    /// @tparam Result Type of the analysis result.
    /// @param id Identifier of the analysis to run.
    /// @return Reference to the cached or freshly computed result.
    template <typename Result> Result &getModuleResult(const std::string &id)
    {
        assert(moduleAnalyses_ && "no module analyses registered");
        auto it = moduleAnalyses_->find(id);
        assert(it != moduleAnalyses_->end() && "unknown module analysis");
        std::any &cache = moduleCache_[id];
        if (!cache.has_value())
        {
            cache = it->second.compute(module_);
            ++counts_.moduleComputations;
        }
        assert(it->second.type == std::type_index(typeid(Result)) &&
               "analysis result type mismatch");
        auto *value = std::any_cast<Result>(&cache);
        assert(value && "analysis result cast failed");
        return *value;
    }

    /// @brief Retrieve or compute a function-level analysis result.
    /// @tparam Result Type of the analysis result.
    /// @param id Identifier of the analysis to run.
    /// @param fn Function to analyze.
    /// @return Reference to the cached or freshly computed result.
    template <typename Result> Result &getFunctionResult(const std::string &id, core::Function &fn)
    {
        assert(functionAnalyses_ && "no function analyses registered");
        auto it = functionAnalyses_->find(id);
        assert(it != functionAnalyses_->end() && "unknown function analysis");
        std::any &cache = functionCache_[id][&fn];
        if (!cache.has_value())
        {
            cache = it->second.compute(module_, fn);
            ++counts_.functionComputations;
        }
        assert(it->second.type == std::type_index(typeid(Result)) &&
               "analysis result type mismatch");
        auto *value = std::any_cast<Result>(&cache);
        assert(value && "analysis result cast failed");
        return *value;
    }

    /// @brief Invalidate analyses not preserved by a module pass.
    /// @param preserved Preservation info returned by the module pass.
    void invalidateAfterModulePass(const PreservedAnalyses &preserved);

    /// @brief Invalidate analyses not preserved by a function pass.
    /// @param preserved Preservation info returned by the function pass.
    /// @param fn Function that was transformed.
    void invalidateAfterFunctionPass(const PreservedAnalyses &preserved, core::Function &fn);

    /// @brief Get mutable access to the module.
    /// @return Reference to the managed module.
    core::Module &module()
    {
        return module_;
    }

    /// @brief Get const access to the module.
    /// @return Const reference to the managed module.
    const core::Module &module() const
    {
        return module_;
    }

    /// @brief Snapshot analysis computation counts for diagnostics.
    /// @return Number of module and function analyses computed so far.
    AnalysisCounts counts() const
    {
        return counts_;
    }

  private:
    core::Module &module_;
    const ModuleAnalysisMap *moduleAnalyses_ = nullptr;
    const FunctionAnalysisMap *functionAnalyses_ = nullptr;
    std::unordered_map<std::string, std::any> moduleCache_;
    std::unordered_map<std::string, std::unordered_map<const core::Function *, std::any>>
        functionCache_;
    AnalysisCounts counts_{};

    friend class AnalysisCacheInvalidator;
};

} // namespace il::transform
