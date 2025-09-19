// File: src/il/transform/PassManager.cpp
// Purpose: Implement the modular IL pass manager and analysis caching.
// Key invariants: Pipelines execute in registration order; analyses obey preservation contracts.
// Ownership/Lifetime: PassManager owns factories; AnalysisManager caches live within a run.
// Links: docs/class-catalog.md

#include "il/transform/PassManager.hpp"

#include "Analysis/CFG.h"
#include "Analysis/Dominators.h"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"
#include <utility>

using namespace il::core;

namespace il::transform
{
namespace
{
struct BlockState
{
    std::unordered_set<unsigned> defs;
    std::unordered_set<unsigned> uses;
    std::unordered_set<unsigned> liveIn;
    std::unordered_set<unsigned> liveOut;
};

CFGInfo buildCFG(core::Module &, core::Function &fn)
{
    CFGInfo info;
    std::unordered_map<std::string, BasicBlock *> labelMap;
    for (auto &block : fn.blocks)
    {
        labelMap[block.label] = &block;
        info.successors[&block];
        info.predecessors[&block];
    }

    for (auto &block : fn.blocks)
    {
        if (block.instructions.empty())
            continue;
        const Instr &term = block.instructions.back();
        if (term.op != Opcode::Br && term.op != Opcode::CBr)
            continue;
        for (const auto &label : term.labels)
        {
            auto it = labelMap.find(label);
            if (it == labelMap.end())
                continue;
            info.successors[&block].push_back(it->second);
            info.predecessors[it->second].push_back(&block);
        }
    }
    return info;
}

LivenessInfo computeLiveness(core::Module &module, core::Function &fn)
{
    CFGInfo cfg = buildCFG(module, fn);
    std::unordered_map<const BasicBlock *, BlockState> states;

    for (auto &block : fn.blocks)
    {
        BlockState &state = states[&block];
        for (const auto &param : block.params)
            state.defs.insert(param.id);

        for (const auto &instr : block.instructions)
        {
            for (const auto &operand : instr.operands)
            {
                if (operand.kind == Value::Kind::Temp && !state.defs.count(operand.id))
                    state.uses.insert(operand.id);
            }
            for (const auto &argList : instr.brArgs)
            {
                for (const auto &arg : argList)
                {
                    if (arg.kind == Value::Kind::Temp && !state.defs.count(arg.id))
                        state.uses.insert(arg.id);
                }
            }
            if (instr.result)
                state.defs.insert(*instr.result);
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto it = fn.blocks.rbegin(); it != fn.blocks.rend(); ++it)
        {
            const BasicBlock *block = &*it;
            BlockState &state = states[block];

            std::unordered_set<unsigned> newOut;
            for (const BasicBlock *succ : cfg.successors[block])
            {
                BlockState &succState = states[succ];
                newOut.insert(succState.liveIn.begin(), succState.liveIn.end());
            }
            if (newOut != state.liveOut)
            {
                state.liveOut = std::move(newOut);
                changed = true;
            }

            std::unordered_set<unsigned> newIn = state.uses;
            for (unsigned value : state.liveOut)
            {
                if (!state.defs.count(value))
                    newIn.insert(value);
            }
            if (newIn != state.liveIn)
            {
                state.liveIn = std::move(newIn);
                changed = true;
            }
        }
    }

    LivenessInfo info;
    for (auto &[block, state] : states)
    {
        info.liveIn[block] = state.liveIn;
        info.liveOut[block] = state.liveOut;
    }
    return info;
}

class LambdaModulePass : public ModulePass
{
  public:
    LambdaModulePass(std::string id, PassManager::ModulePassCallback cb)
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
    PassManager::ModulePassCallback callback_;
};

class LambdaFunctionPass : public FunctionPass
{
  public:
    LambdaFunctionPass(std::string id, PassManager::FunctionPassCallback cb)
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
    PassManager::FunctionPassCallback callback_;
};

} // namespace

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
    return preserveAllModules_ || !moduleAnalyses_.empty();
}

bool PreservedAnalyses::hasFunctionPreservations() const
{
    return preserveAllFunctions_ || !functionAnalyses_.empty();
}

AnalysisManager::AnalysisManager(core::Module &module,
                                 const ModuleAnalysisMap &moduleAnalyses,
                                 const FunctionAnalysisMap &functionAnalyses)
    : module_(module), moduleAnalyses_(&moduleAnalyses), functionAnalyses_(&functionAnalyses)
{
}

void AnalysisManager::invalidateAfterModulePass(const PreservedAnalyses &preserved)
{
    if (!preserved.preservesAllModuleAnalyses())
    {
        if (!preserved.hasModulePreservations())
        {
            moduleCache_.clear();
        }
        else
        {
            for (auto it = moduleCache_.begin(); it != moduleCache_.end();)
            {
                if (preserved.isModulePreserved(it->first))
                    ++it;
                else
                    it = moduleCache_.erase(it);
            }
        }
    }

    if (!preserved.preservesAllFunctionAnalyses())
    {
        if (!preserved.hasFunctionPreservations())
        {
            functionCache_.clear();
        }
        else
        {
            for (auto it = functionCache_.begin(); it != functionCache_.end();)
            {
                if (preserved.isFunctionPreserved(it->first))
                {
                    ++it;
                }
                else
                {
                    it = functionCache_.erase(it);
                }
            }
        }
    }
}

void AnalysisManager::invalidateAfterFunctionPass(const PreservedAnalyses &preserved, core::Function &fn)
{
    if (!preserved.preservesAllModuleAnalyses())
    {
        if (!preserved.hasModulePreservations())
        {
            moduleCache_.clear();
        }
        else
        {
            for (auto it = moduleCache_.begin(); it != moduleCache_.end();)
            {
                if (preserved.isModulePreserved(it->first))
                    ++it;
                else
                    it = moduleCache_.erase(it);
            }
        }
    }

    if (!preserved.preservesAllFunctionAnalyses())
    {
        if (!preserved.hasFunctionPreservations())
        {
            for (auto it = functionCache_.begin(); it != functionCache_.end();)
            {
                it->second.erase(&fn);
                if (it->second.empty())
                    it = functionCache_.erase(it);
                else
                    ++it;
            }
        }
        else
        {
            for (auto it = functionCache_.begin(); it != functionCache_.end();)
            {
                if (preserved.isFunctionPreserved(it->first))
                {
                    ++it;
                    continue;
                }
                it->second.erase(&fn);
                if (it->second.empty())
                    it = functionCache_.erase(it);
                else
                    ++it;
            }
        }
    }
}

PassManager::PassManager()
{
#ifndef NDEBUG
    verifyBetweenPasses_ = true;
#else
    verifyBetweenPasses_ = false;
#endif

    registerFunctionAnalysis<CFGInfo>("cfg",
                                      [](core::Module &module, core::Function &fn) { return buildCFG(module, fn); });
    registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](core::Module &module, core::Function &fn)
        {
            viper::analysis::setModule(module);
            return viper::analysis::computeDominatorTree(fn);
        });
    registerFunctionAnalysis<LivenessInfo>("liveness",
                                           [](core::Module &module, core::Function &fn)
                                           { return computeLiveness(module, fn); });
}

void PassManager::registerModulePass(const std::string &id, ModulePassFactory factory)
{
    passRegistry_[id] = detail::PassFactory{detail::PassKind::Module, std::move(factory), {}};
}

void PassManager::registerModulePass(const std::string &id, ModulePassCallback callback)
{
    auto cb = ModulePassCallback(callback);
    passRegistry_[id] = detail::PassFactory{
        detail::PassKind::Module,
        [passId = std::string(id), cb]() { return std::make_unique<LambdaModulePass>(passId, cb); },
        {}};
}

void PassManager::registerModulePass(const std::string &id, const std::function<void(core::Module &)> &fn)
{
    registerModulePass(id, [fn](core::Module &module, AnalysisManager &) {
        fn(module);
        return PreservedAnalyses::none();
    });
}

void PassManager::registerFunctionPass(const std::string &id, FunctionPassFactory factory)
{
    passRegistry_[id] = detail::PassFactory{detail::PassKind::Function, {}, std::move(factory)};
}

void PassManager::registerFunctionPass(const std::string &id, FunctionPassCallback callback)
{
    auto cb = FunctionPassCallback(callback);
    passRegistry_[id] = detail::PassFactory{
        detail::PassKind::Function,
        {},
        [passId = std::string(id), cb]() { return std::make_unique<LambdaFunctionPass>(passId, cb); }};
}

void PassManager::registerFunctionPass(const std::string &id, const std::function<void(core::Function &)> &fn)
{
    registerFunctionPass(id, [fn](core::Function &function, AnalysisManager &) {
        fn(function);
        return PreservedAnalyses::none();
    });
}

void PassManager::registerPipeline(const std::string &id, Pipeline pipeline)
{
    pipelines_[id] = std::move(pipeline);
}

const PassManager::Pipeline *PassManager::getPipeline(const std::string &id) const
{
    auto it = pipelines_.find(id);
    return it == pipelines_.end() ? nullptr : &it->second;
}

void PassManager::setVerifyBetweenPasses(bool enable)
{
    verifyBetweenPasses_ = enable;
}

void PassManager::run(core::Module &module, const Pipeline &pipeline) const
{
    AnalysisManager analysis(module, moduleAnalyses_, functionAnalyses_);

    for (const auto &passId : pipeline)
    {
        auto it = passRegistry_.find(passId);
        if (it == passRegistry_.end())
            continue;

        const detail::PassFactory &factory = it->second;
        switch (factory.kind)
        {
            case detail::PassKind::Module:
            {
                if (!factory.makeModule)
                    break;
                auto pass = factory.makeModule();
                if (!pass)
                    break;
                PreservedAnalyses preserved = pass->run(module, analysis);
                analysis.invalidateAfterModulePass(preserved);
                break;
            }
            case detail::PassKind::Function:
            {
                if (!factory.makeFunction)
                    break;
                for (auto &fn : module.functions)
                {
                    auto pass = factory.makeFunction();
                    if (!pass)
                        continue;
                    PreservedAnalyses preserved = pass->run(fn, analysis);
                    analysis.invalidateAfterFunctionPass(preserved, fn);
                }
                break;
            }
        }

#ifndef NDEBUG
        if (verifyBetweenPasses_)
        {
            auto verified = il::verify::Verifier::verify(module);
            assert(verified && "IL verification failed after pass");
        }
#endif
    }
}

bool PassManager::runPipeline(core::Module &module, const std::string &pipelineId) const
{
    const Pipeline *pipeline = getPipeline(pipelineId);
    if (!pipeline)
        return false;
    run(module, *pipeline);
    return true;
}

} // namespace il::transform

