// File: src/il/transform/PassRegistry.cpp
// License: MIT License. See LICENSE in the project root for details.
// Purpose: Implement pass registration plumbing and preservation tracking for IL transforms.
// Key invariants: Pass factories remain valid for the program lifetime; preservation flags guard caches.
// Ownership/Lifetime: Factories and callbacks are stored by value and invoked during pipeline execution.
// Links: docs/codemap.md

#include "il/transform/PassRegistry.hpp"

#include "il/transform/AnalysisManager.hpp"

#include <utility>

namespace il::transform
{

PreservedAnalyses PreservedAnalyses::all()
{
    PreservedAnalyses p;
    p.preserveAllModules_ = true;
    p.preserveAllFunctions_ = true;
    return p;
}

PreservedAnalyses PreservedAnalyses::none()
{
    return PreservedAnalyses{};
}

PreservedAnalyses &PreservedAnalyses::preserveModule(const std::string &id)
{
    moduleAnalyses_.insert(id);
    return *this;
}

PreservedAnalyses &PreservedAnalyses::preserveFunction(const std::string &id)
{
    functionAnalyses_.insert(id);
    return *this;
}

PreservedAnalyses &PreservedAnalyses::preserveAllModules()
{
    preserveAllModules_ = true;
    return *this;
}

PreservedAnalyses &PreservedAnalyses::preserveAllFunctions()
{
    preserveAllFunctions_ = true;
    return *this;
}

bool PreservedAnalyses::preservesAllModuleAnalyses() const
{
    return preserveAllModules_;
}

bool PreservedAnalyses::preservesAllFunctionAnalyses() const
{
    return preserveAllFunctions_;
}

bool PreservedAnalyses::isModulePreserved(const std::string &id) const
{
    return preserveAllModules_ || moduleAnalyses_.count(id) > 0;
}

bool PreservedAnalyses::isFunctionPreserved(const std::string &id) const
{
    return preserveAllFunctions_ || functionAnalyses_.count(id) > 0;
}

bool PreservedAnalyses::hasModulePreservations() const
{
    return !moduleAnalyses_.empty();
}

bool PreservedAnalyses::hasFunctionPreservations() const
{
    return !functionAnalyses_.empty();
}

namespace
{
class LambdaModulePass : public ModulePass
{
  public:
    LambdaModulePass(std::string id, PassRegistry::ModulePassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    std::string_view id() const override { return id_; }

    PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) override
    {
        return callback_(module, analysis);
    }

  private:
    std::string id_;
    PassRegistry::ModulePassCallback callback_;
};

class LambdaFunctionPass : public FunctionPass
{
  public:
    LambdaFunctionPass(std::string id, PassRegistry::FunctionPassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    std::string_view id() const override { return id_; }

    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override
    {
        return callback_(function, analysis);
    }

  private:
    std::string id_;
    PassRegistry::FunctionPassCallback callback_;
};
} // namespace

void PassRegistry::registerModulePass(const std::string &id, ModulePassFactory factory)
{
    registry_[id] = detail::PassFactory{detail::PassKind::Module, std::move(factory), {}};
}

void PassRegistry::registerModulePass(const std::string &id, ModulePassCallback callback)
{
    auto cb = ModulePassCallback(callback);
    registry_[id] = detail::PassFactory{
        detail::PassKind::Module,
        [passId = std::string(id), cb]() { return std::make_unique<LambdaModulePass>(passId, cb); },
        {}};
}

void PassRegistry::registerModulePass(const std::string &id,
                                      const std::function<void(core::Module &)> &fn)
{
    registerModulePass(id,
                       [fn](core::Module &module, AnalysisManager &)
                       {
                           fn(module);
                           return PreservedAnalyses::none();
                       });
}

void PassRegistry::registerFunctionPass(const std::string &id, FunctionPassFactory factory)
{
    registry_[id] = detail::PassFactory{detail::PassKind::Function, {}, std::move(factory)};
}

void PassRegistry::registerFunctionPass(const std::string &id, FunctionPassCallback callback)
{
    auto cb = FunctionPassCallback(callback);
    registry_[id] = detail::PassFactory{
        detail::PassKind::Function,
        {},
        [passId = std::string(id), cb]() { return std::make_unique<LambdaFunctionPass>(passId, cb); }};
}

void PassRegistry::registerFunctionPass(const std::string &id,
                                        const std::function<void(core::Function &)> &fn)
{
    registerFunctionPass(id,
                         [fn](core::Function &function, AnalysisManager &)
                         {
                             fn(function);
                             return PreservedAnalyses::none();
                         });
}

const detail::PassFactory *PassRegistry::lookup(std::string_view id) const
{
    auto it = registry_.find(std::string(id));
    if (it == registry_.end())
        return nullptr;
    return &it->second;
}

} // namespace il::transform

