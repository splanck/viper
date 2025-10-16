// File: src/il/transform/AnalysisManager.cpp
// License: MIT License. See LICENSE in the project root for details.
// Purpose: Implement analysis caching utilities and invalidation helpers for IL transforms.
// Key invariants: Preservation summaries only retain caches explicitly marked as valid.
// Ownership/Lifetime: AnalysisManager borrows module references and clears caches per pipeline run.
// Links: docs/codemap.md

#include "il/transform/AnalysisManager.hpp"

#include "il/transform/PassRegistry.hpp"

#include <utility>

namespace il::transform
{
class AnalysisCacheInvalidator
{
  public:
    AnalysisCacheInvalidator(AnalysisManager &manager, const PreservedAnalyses &preserved)
        : manager_(manager), preserved_(preserved)
    {
    }

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

AnalysisManager::AnalysisManager(core::Module &module, const AnalysisRegistry &registry)
    : module_(module)
{
    moduleAnalyses_ = &registry.moduleAnalyses();
    functionAnalyses_ = &registry.functionAnalyses();
}

void AnalysisManager::invalidateAfterModulePass(const PreservedAnalyses &preserved)
{
    AnalysisCacheInvalidator(*this, preserved).afterModulePass();
}

void AnalysisManager::invalidateAfterFunctionPass(const PreservedAnalyses &preserved, core::Function &fn)
{
    AnalysisCacheInvalidator(*this, preserved).afterFunctionPass(fn);
}

} // namespace il::transform

