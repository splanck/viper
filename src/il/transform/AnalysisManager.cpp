//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the bookkeeping utilities responsible for caching IL analyses and
// invalidating them after transform passes execute.  AnalysisManager maintains
// per-module and per-function caches keyed by the identifiers registered in the
// pass registry.  This file centralises the invalidation logic so preservation
// summaries only need to describe which analyses remain valid.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"

#include "il/transform/PassRegistry.hpp"

#include <utility>

namespace il::transform
{
class AnalysisCacheInvalidator
{
  public:
    /// @brief Prepare an invalidator for the given manager/preservation pair.
    ///
    /// The helper stores references to the owning AnalysisManager and the
    /// preservation summary returned by a pass.  Subsequent calls use these
    /// references to decide which cached analyses to evict.
    ///
    /// @param manager Manager whose caches will be pruned.
    /// @param preserved Summary describing which analyses remain valid.
    AnalysisCacheInvalidator(AnalysisManager &manager, const PreservedAnalyses &preserved)
        : manager_(manager), preserved_(preserved)
    {
    }

    /// @brief Invalidate module-scoped analyses according to preservation data.
    ///
    /// When no module analyses were ever registered the call is a no-op.  If a
    /// pass preserved every analysis the cache is untouched.  Otherwise the
    /// routine either clears the entire cache or erases just the analyses that
    /// were not marked as preserved.
    void afterModulePass()
    {
        if (!manager_.moduleAnalyses_)
            return;
        if (preserved_.preservesAllModuleAnalyses())
            return;
        if (!preserved_.hasModulePreservations())
        {
            manager_.moduleCache_.clear();
            return;
        }

        for (auto it = manager_.moduleCache_.begin(); it != manager_.moduleCache_.end();)
        {
            if (preserved_.isModulePreserved(it->first))
            {
                ++it;
                continue;
            }
            it = manager_.moduleCache_.erase(it);
        }
    }

    /// @brief Invalidate function-scoped analyses for a specific function.
    ///
    /// Behaviour mirrors @ref AnalysisCacheInvalidator::afterModulePass but operates on the
    /// per-function caches.  When only a subset of analyses are preserved, the routine walks each
    /// cache entry and removes the stale data for @p fn.  Empty analysis maps are pruned to keep
    /// the cache compact.
    ///
    /// @param fn Function whose cached analyses should be reviewed.
    void afterFunctionPass(core::Function &fn)
    {
        if (!manager_.functionAnalyses_)
            return;
        if (preserved_.preservesAllFunctionAnalyses())
            return;
        if (!preserved_.hasFunctionPreservations())
        {
            for (auto &entry : manager_.functionCache_)
                entry.second.erase(&fn);
            return;
        }

        for (auto it = manager_.functionCache_.begin(); it != manager_.functionCache_.end();)
        {
            if (preserved_.isFunctionPreserved(it->first))
            {
                ++it;
                continue;
            }
            it->second.erase(&fn);
            if (it->second.empty())
                it = manager_.functionCache_.erase(it);
            else
                ++it;
        }
    }

  private:
    AnalysisManager &manager_;
    const PreservedAnalyses &preserved_;
};

/// @brief Construct an analysis manager tied to a module and registry.
///
/// The constructor captures the module reference and caches pointers to the
/// module/function analysis registries so that lookups avoid repeated
/// indirection during pipeline execution.
///
/// @param module Module undergoing transformation.
/// @param registry Registry describing the available analyses.
AnalysisManager::AnalysisManager(core::Module &module, const AnalysisRegistry &registry)
    : module_(module)
{
    moduleAnalyses_ = &registry.moduleAnalyses();
    functionAnalyses_ = &registry.functionAnalyses();
}

/// @brief Apply invalidation logic after a module pass has completed.
///
/// Wraps the helper class so callers can simply forward the preservation summary
/// returned by the pass.
///
/// @param preserved Summary of analyses that remain valid.
void AnalysisManager::invalidateAfterModulePass(const PreservedAnalyses &preserved)
{
    AnalysisCacheInvalidator(*this, preserved).afterModulePass();
}

/// @brief Apply invalidation logic after a function pass has completed.
///
/// @param preserved Summary of analyses that remain valid.
/// @param fn Function whose cached analyses should be pruned.
void AnalysisManager::invalidateAfterFunctionPass(const PreservedAnalyses &preserved,
                                                  core::Function &fn)
{
    AnalysisCacheInvalidator(*this, preserved).afterFunctionPass(fn);
}

} // namespace il::transform
