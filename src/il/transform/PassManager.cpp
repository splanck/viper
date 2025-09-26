// File: src/il/transform/PassManager.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// MIT License Notice: Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions. The Software is provided "AS IS", without warranty of any kind, express or
// implied, including but not limited to the warranties of merchantability, fitness for a
// particular purpose and noninfringement.
// Purpose: Implement the modular IL pass manager and analysis caching.
// Key invariants: Pipelines execute in registration order; analyses obey preservation contracts.
// Ownership/Lifetime: PassManager owns factories; AnalysisManager caches live within a run.
// Links: docs/codemap.md

#include "il/transform/PassManager.hpp"

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include <algorithm>
#include <cassert>
#include <utility>

using namespace il::core;

namespace il::transform
{
/// @brief Query whether a value identifier is marked live.
/// @param valueId Dense identifier for the SSA value.
/// @return True when the identifier is within range and flagged live.
bool LivenessInfo::SetView::contains(unsigned valueId) const
{
    return bits_ != nullptr && valueId < bits_->size() && (*bits_)[valueId];
}

/// @brief Check whether the set contains any live values.
/// @return True when no value bits are set.
bool LivenessInfo::SetView::empty() const
{
    return bits_ == nullptr ||
           std::none_of(bits_->begin(), bits_->end(), [](bool bit) { return bit; });
}

/// @brief Access the underlying bitset for integration tests or debugging.
/// @return Reference to the immutable bitset representation.
const std::vector<bool> &LivenessInfo::SetView::bits() const
{
    assert(bits_ && "liveness set view is empty");
    return *bits_;
}

LivenessInfo::SetView::SetView(const std::vector<bool> *bits) : bits_(bits) {}

/// @brief Retrieve the live-in set for @p block.
/// @param block Basic block whose entry set is requested.
/// @return Lightweight view over the live-in values.
LivenessInfo::SetView LivenessInfo::liveIn(const core::BasicBlock &block) const
{
    return liveIn(&block);
}

/// @brief Retrieve the live-in set for @p block.
/// @param block Basic block pointer whose entry set is requested.
/// @return Lightweight view over the live-in values.
LivenessInfo::SetView LivenessInfo::liveIn(const core::BasicBlock *block) const
{
    if (!block)
        return SetView();
    auto it = liveInBits_.find(block);
    if (it == liveInBits_.end())
        return SetView();
    return SetView(&it->second);
}

/// @brief Retrieve the live-out set for @p block.
/// @param block Basic block whose exit set is requested.
/// @return Lightweight view over the live-out values.
LivenessInfo::SetView LivenessInfo::liveOut(const core::BasicBlock &block) const
{
    return liveOut(&block);
}

/// @brief Retrieve the live-out set for @p block pointer.
/// @param block Basic block pointer whose exit set is requested.
/// @return Lightweight view over the live-out values.
LivenessInfo::SetView LivenessInfo::liveOut(const core::BasicBlock *block) const
{
    if (!block)
        return SetView();
    auto it = liveOutBits_.find(block);
    if (it == liveOutBits_.end())
        return SetView();
    return SetView(&it->second);
}

/// @brief Number of dense SSA value slots tracked by this liveness summary.
/// @return Count of bits allocated per block.
std::size_t LivenessInfo::valueCount() const
{
    return valueCount_;
}

namespace
{
struct BlockInfo
{
    explicit BlockInfo(std::size_t valueCount = 0)
        : defs(valueCount, false), uses(valueCount, false)
    {
    }

    std::vector<bool> defs;
    std::vector<bool> uses;
};

std::size_t determineValueCapacity(const core::Function &fn)
{
    unsigned maxId = 0;
    bool sawId = false;
    auto noteId = [&](unsigned id)
    {
        maxId = std::max(maxId, id);
        sawId = true;
    };

    for (const auto &param : fn.params)
        noteId(param.id);

    for (const auto &block : fn.blocks)
    {
        for (const auto &param : block.params)
            noteId(param.id);

        for (const auto &instr : block.instructions)
        {
            for (const auto &operand : instr.operands)
            {
                if (operand.kind == Value::Kind::Temp)
                    noteId(operand.id);
            }
            for (const auto &argList : instr.brArgs)
            {
                for (const auto &arg : argList)
                {
                    if (arg.kind == Value::Kind::Temp)
                        noteId(arg.id);
                }
            }
            if (instr.result)
                noteId(*instr.result);
        }
    }

    std::size_t capacity = sawId ? static_cast<std::size_t>(maxId) + 1 : 0;
    if (fn.valueNames.size() > capacity)
        capacity = fn.valueNames.size();
    return capacity;
}

inline void setBit(std::vector<bool> &bits, unsigned id)
{
    assert(id < bits.size());
    if (id < bits.size())
        bits[id] = true;
}

inline void mergeBits(std::vector<bool> &dst, const std::vector<bool> &src)
{
    assert(dst.size() == src.size());
    for (std::size_t idx = 0; idx < dst.size(); ++idx)
    {
        if (src[idx])
            dst[idx] = true;
    }
}

} // namespace

/// @brief Construct predecessor and successor relationships for a function.
/// @details Creates an adjacency entry for each basic block and delegates to
/// `analysis::successors`/`predecessors` so branch decoding lives in a single
/// implementation. The function only mutates the adjacency lists in the local
/// CFGInfo; @p module and @p fn are not modified.
/// @param module Module that owns @p fn (unused, maintained for signature parity).
/// @param fn Function whose control-flow graph is being synthesized.
/// @return CFG adjacency information for all blocks in @p fn.
CFGInfo buildCFG(core::Module &module, core::Function &fn)
{
    CFGInfo info;
    viper::analysis::CFGContext ctx(module);

    for (auto &block : fn.blocks)
    {
        auto &succ = info.successors[&block];
        auto succBlocks = viper::analysis::successors(ctx, block);
        succ.reserve(succBlocks.size());
        for (auto *succBlock : succBlocks)
            succ.push_back(succBlock);
    }

    for (auto &block : fn.blocks)
    {
        auto &pred = info.predecessors[&block];
        auto predBlocks = viper::analysis::predecessors(ctx, block);
        pred.reserve(predBlocks.size());
        for (auto *predBlock : predBlocks)
            pred.push_back(predBlock);
    }

    return info;
}

/// @brief Compute liveness information for each block within @p fn.
/// @details Consumes the adjacency stored in @p cfg, records block-local def
/// and use bitsets, and iterates a backward data-flow fixpoint that merges
/// successor live-in sets until convergence. Intermediate state is stored in a
/// temporary table keyed by basic block pointers while the resulting live-in
/// and live-out sets are written directly into the returned LivenessInfo.
/// @param module Owning module used for analysis context (unused).
/// @param fn Function whose live-in/live-out sets should be determined.
/// @param cfg Cached CFG adjacency information shared with other analyses.
/// @return Liveness summary describing values live on block entry and exit.
LivenessInfo computeLiveness(core::Module &module, core::Function &fn, const CFGInfo &cfg)
{
    static_cast<void>(module);
    const std::size_t valueCount = determineValueCapacity(fn);

    std::unordered_map<const BasicBlock *, BlockInfo> blockInfo;
    blockInfo.reserve(fn.blocks.size());

    LivenessInfo info;
    info.valueCount_ = valueCount;

    for (auto &block : fn.blocks)
    {
        BlockInfo state(valueCount);

        for (const auto &param : block.params)
            setBit(state.defs, param.id);

        for (const auto &instr : block.instructions)
        {
            for (const auto &operand : instr.operands)
            {
                if (operand.kind != Value::Kind::Temp)
                    continue;
                const unsigned id = operand.id;
                if (id >= valueCount)
                    continue;
                if (!state.defs[id])
                    state.uses[id] = true;
            }
            for (const auto &argList : instr.brArgs)
            {
                for (const auto &arg : argList)
                {
                    if (arg.kind != Value::Kind::Temp)
                        continue;
                    const unsigned id = arg.id;
                    if (id >= valueCount)
                        continue;
                    if (!state.defs[id])
                        state.uses[id] = true;
                }
            }
            if (instr.result)
                setBit(state.defs, *instr.result);
        }

        blockInfo.emplace(&block, std::move(state));
        info.liveInBits_.emplace(&block, std::vector<bool>(valueCount, false));
        info.liveOutBits_.emplace(&block, std::vector<bool>(valueCount, false));
    }

    std::vector<bool> scratchOut(valueCount, false);
    std::vector<bool> scratchIn(valueCount, false);

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto it = fn.blocks.rbegin(); it != fn.blocks.rend(); ++it)
        {
            const BasicBlock *block = &*it;
            auto stateIt = blockInfo.find(block);
            assert(stateIt != blockInfo.end());
            BlockInfo &state = stateIt->second;

            auto &liveOut = info.liveOutBits_[block];
            std::fill(scratchOut.begin(), scratchOut.end(), false);
            auto succIt = cfg.successors.find(block);
            if (succIt != cfg.successors.end())
            {
                for (const BasicBlock *succ : succIt->second)
                {
                    auto liveInIt = info.liveInBits_.find(succ);
                    if (liveInIt == info.liveInBits_.end())
                        continue;
                    mergeBits(scratchOut, liveInIt->second);
                }
            }
            if (scratchOut != liveOut)
            {
                liveOut = scratchOut;
                changed = true;
            }

            scratchIn = state.uses;
            for (std::size_t idx = 0; idx < valueCount; ++idx)
            {
                if (liveOut[idx] && !state.defs[idx])
                    scratchIn[idx] = true;
            }
            auto &liveIn = info.liveInBits_[block];
            if (scratchIn != liveIn)
            {
                liveIn = scratchIn;
                changed = true;
            }
        }
    }

    return info;
}

LivenessInfo computeLiveness(core::Module &module, core::Function &fn)
{
    CFGInfo cfg = buildCFG(module, fn);
    return computeLiveness(module, fn, cfg);
}

namespace
{
/// @brief Adapter that wraps a module-pass callback into the ModulePass API.
/// @details Captures a module pass identifier and callback so the pass manager
/// can lazily produce ModulePass instances. Construction mutates only the
/// adapter's identifier and callback members; the wrapped callable controls
/// module mutation when invoked.
class LambdaModulePass : public ModulePass
{
  public:
    /// @brief Construct an adapter around the provided callback and identifier.
    /// @details Moves @p id and @p cb into the adapter's private members so they
    /// remain available for subsequent runs.
    LambdaModulePass(std::string id, PassManager::ModulePassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    /// @brief Expose the identifier assigned to the wrapped module pass.
    /// @details Returns a view of the cached identifier without mutating state
    /// so the pass manager can report which pass is executing.
    std::string_view id() const override
    {
        return id_;
    }

    /// @brief Invoke the wrapped module callback.
    /// @details Forwards @p module and @p analysis to the stored callback which
    /// may mutate the module and reports preservation state. The adapter keeps
    /// its cached identifier and callback unchanged.
    PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) override
    {
        return callback_(module, analysis);
    }

  private:
    std::string id_;
    PassManager::ModulePassCallback callback_;
};

/// @brief Adapter that wraps a function-pass callback into the FunctionPass API.
/// @details Captures a function pass identifier and callback so the pass manager
/// can instantiate lightweight FunctionPass objects on demand. Construction
/// mutates only the stored identifier and callback; function bodies are mutated
/// by the wrapped callable at run time.
class LambdaFunctionPass : public FunctionPass
{
  public:
    /// @brief Construct an adapter around the provided callback and identifier.
    /// @details Moves @p id and @p cb into the adapter to preserve them for
    /// subsequent invocations.
    LambdaFunctionPass(std::string id, PassManager::FunctionPassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    /// @brief Expose the identifier assigned to the wrapped function pass.
    /// @details Returns a view of the cached identifier without modifying
    /// adapter state, supporting pass diagnostics.
    std::string_view id() const override
    {
        return id_;
    }

    /// @brief Invoke the wrapped function callback.
    /// @details Forwards @p function and @p analysis to the stored callback,
    /// allowing it to mutate the function and describe preserved analyses while
    /// leaving the adapter's cached state untouched.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override
    {
        return callback_(function, analysis);
    }

  private:
    std::string id_;
    PassManager::FunctionPassCallback callback_;
};

} // namespace

/// @brief Return a preservation set that keeps all analyses valid.
/// @details Sets both global preservation flags inside a local PreservedAnalyses
/// instance and returns it, signalling that downstream passes should keep every
/// cached analysis result.
PreservedAnalyses PreservedAnalyses::all()
{
    PreservedAnalyses p;
    p.preserveAllModules_ = true;
    p.preserveAllFunctions_ = true;
    return p;
}

/// @brief Return a preservation set that invalidates every analysis.
/// @details Returns a default-constructed PreservedAnalyses where no flags or
/// explicit preservations are set, instructing the analysis manager to drop all
/// cached data.
PreservedAnalyses PreservedAnalyses::none()
{
    return PreservedAnalyses{};
}

/// @brief Preserve a single module-level analysis by identifier.
/// @details Inserts @p id into the module preservation set so invalidate
/// routines retain that cached analysis.
/// @param id Registered analysis identifier to retain.
/// @return Reference to the updated preservation summary.
PreservedAnalyses &PreservedAnalyses::preserveModule(const std::string &id)
{
    moduleAnalyses_.insert(id);
    return *this;
}

/// @brief Preserve a single function-level analysis by identifier.
/// @details Inserts @p id into the function preservation set so cached
/// per-function analyses remain valid across passes.
/// @param id Registered analysis identifier to retain.
/// @return Reference to the updated preservation summary.
PreservedAnalyses &PreservedAnalyses::preserveFunction(const std::string &id)
{
    functionAnalyses_.insert(id);
    return *this;
}

/// @brief Mark that all module analyses remain valid after a pass.
/// @details Sets the `preserveAllModules_` flag so cache invalidation can skip
/// module analyses entirely.
/// @return Reference to the updated preservation summary.
PreservedAnalyses &PreservedAnalyses::preserveAllModules()
{
    preserveAllModules_ = true;
    return *this;
}

/// @brief Mark that all function analyses remain valid after a pass.
/// @details Sets the `preserveAllFunctions_` flag so cache invalidation can skip
/// per-function analyses entirely.
/// @return Reference to the updated preservation summary.
PreservedAnalyses &PreservedAnalyses::preserveAllFunctions()
{
    preserveAllFunctions_ = true;
    return *this;
}

/// @brief Determine whether every module analysis remains preserved.
/// @details Returns the value of `preserveAllModules_` without mutating state,
/// allowing callers to avoid further module analysis checks when true.
bool PreservedAnalyses::preservesAllModuleAnalyses() const
{
    return preserveAllModules_;
}

/// @brief Determine whether every function analysis remains preserved.
/// @details Returns the value of `preserveAllFunctions_` without mutating
/// state, allowing callers to avoid further function analysis checks when true.
bool PreservedAnalyses::preservesAllFunctionAnalyses() const
{
    return preserveAllFunctions_;
}

/// @brief Check whether a specific module analysis was preserved.
/// @details Returns true when either the global module flag is set or @p id is
/// present in the explicit module preservation set; the query does not mutate
/// the tracked preservation data.
/// @param id Analysis identifier to query.
bool PreservedAnalyses::isModulePreserved(const std::string &id) const
{
    return preserveAllModules_ || moduleAnalyses_.count(id) > 0;
}

/// @brief Check whether a specific function analysis was preserved.
/// @details Returns true when either the global function flag is set or @p id is
/// present in the explicit function preservation set; the query leaves
/// preservation data untouched.
/// @param id Analysis identifier to query.
bool PreservedAnalyses::isFunctionPreserved(const std::string &id) const
{
    return preserveAllFunctions_ || functionAnalyses_.count(id) > 0;
}

/// @brief Determine whether any module analysis was preserved explicitly.
/// @details Reports whether the global module flag is set or the explicit set
/// contains entries without mutating the tracked data, guiding cache
/// invalidation strategies.
bool PreservedAnalyses::hasModulePreservations() const
{
    return preserveAllModules_ || !moduleAnalyses_.empty();
}

/// @brief Determine whether any function analysis was preserved explicitly.
/// @details Reports whether the global function flag is set or the explicit set
/// contains entries without mutating the tracked data, guiding cache
/// invalidation strategies.
bool PreservedAnalyses::hasFunctionPreservations() const
{
    return preserveAllFunctions_ || !functionAnalyses_.empty();
}

/// @brief Construct an analysis manager with registered analysis metadata.
/// @details Stores references to the owning module along with maps describing
/// available module and function analyses so results can be instantiated and
/// cached later. Only internal pointers and references are initialised; caches
/// remain empty until queries occur.
AnalysisManager::AnalysisManager(core::Module &module,
                                 const ModuleAnalysisMap &moduleAnalyses,
                                 const FunctionAnalysisMap &functionAnalyses)
    : module_(module), moduleAnalyses_(&moduleAnalyses), functionAnalyses_(&functionAnalyses)
{
}

/// @brief Invalidate cached results following execution of a module pass.
/// @details Clears or prunes module and function caches depending on the
/// preservation summary returned by the pass. When @p preserved keeps no module
/// analyses, the entire module cache is cleared; otherwise individual entries
/// not flagged as preserved are removed. Function caches follow the same logic
/// but remove entire maps when no analyses are retained. Both cache containers
/// may be mutated in-place.
/// @param preserved Preservation summary describing retained analyses.
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

/// @brief Invalidate cached results following execution of a function pass.
/// @details Applies the module-level invalidation logic and then removes cached
/// function analyses associated with @p fn when they are not preserved. Buckets
/// emptied by the removal are erased, so the per-function cache map is updated
/// in-place.
/// @param preserved Preservation summary describing retained analyses.
/// @param fn Function whose cached analyses should be reconsidered.
void AnalysisManager::invalidateAfterFunctionPass(const PreservedAnalyses &preserved,
                                                  core::Function &fn)
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

/// @brief Construct a pass manager with default analyses and verification state.
/// @details Sets `verifyBetweenPasses_` based on build configuration and
/// registers built-in CFG, dominator tree, and liveness analyses so they are
/// available for later queries. Mutates the pass manager's analysis registry
/// members while leaving the module registry empty for callers to populate.
PassManager::PassManager()
{
#ifndef NDEBUG
    verifyBetweenPasses_ = true;
#else
    verifyBetweenPasses_ = false;
#endif

    registerFunctionAnalysis<CFGInfo>(
        "cfg", [](core::Module &module, core::Function &fn) { return buildCFG(module, fn); });
    registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](core::Module &module, core::Function &fn)
        {
            viper::analysis::CFGContext ctx(module);
            return viper::analysis::computeDominatorTree(ctx, fn);
        });
    registerFunctionAnalysis<LivenessInfo>("liveness",
                                           [](core::Module &module, core::Function &fn)
                                           { return computeLiveness(module, fn); });
}

/// @brief Register a module pass factory under the provided identifier.
/// @details Writes or replaces an entry in `passRegistry_` so later pipeline
/// execution can instantiate module passes via @p factory. Mutates only the
/// registry map.
/// @param id Unique module pass identifier.
/// @param factory Factory constructing a ModulePass each time the pass is scheduled.
void PassManager::registerModulePass(const std::string &id, ModulePassFactory factory)
{
    passRegistry_[id] = detail::PassFactory{detail::PassKind::Module, std::move(factory), {}};
}

/// @brief Register a module pass backed by a callback.
/// @details Wraps @p callback in a LambdaModulePass-producing factory and stores
/// it in `passRegistry_`, enabling pipelines to run the lambda without callers
/// manually creating ModulePass subclasses.
/// @param id Unique module pass identifier.
/// @param callback Callable invoked when the pass executes.
void PassManager::registerModulePass(const std::string &id, ModulePassCallback callback)
{
    auto cb = ModulePassCallback(callback);
    passRegistry_[id] = detail::PassFactory{
        detail::PassKind::Module,
        [passId = std::string(id), cb]() { return std::make_unique<LambdaModulePass>(passId, cb); },
        {}};
}

/// @brief Register a module pass that executes a simple mutator lambda.
/// @details Converts the provided mutator @p fn into a callback-based module
/// pass that always reports `PreservedAnalyses::none`, guaranteeing cached
/// analyses are purged. Updates only the pass registry.
/// @param id Unique module pass identifier.
/// @param fn Callable invoked for each run; assumed to invalidate all analyses.
void PassManager::registerModulePass(const std::string &id,
                                     const std::function<void(core::Module &)> &fn)
{
    registerModulePass(id,
                       [fn](core::Module &module, AnalysisManager &)
                       {
                           fn(module);
                           return PreservedAnalyses::none();
                       });
}

/// @brief Register a function pass factory under the provided identifier.
/// @details Stores @p factory in `passRegistry_` so a new FunctionPass can be
/// materialised for each function when the pass executes. Mutates only the
/// registry map.
/// @param id Unique function pass identifier.
/// @param factory Factory constructing a FunctionPass for each function invocation.
void PassManager::registerFunctionPass(const std::string &id, FunctionPassFactory factory)
{
    passRegistry_[id] = detail::PassFactory{detail::PassKind::Function, {}, std::move(factory)};
}

/// @brief Register a function pass backed by a callback.
/// @details Wraps @p callback in a LambdaFunctionPass-producing factory stored
/// in `passRegistry_`, allowing pipelines to execute the lambda for each
/// function without bespoke FunctionPass classes.
/// @param id Unique function pass identifier.
/// @param callback Callable invoked for each function execution.
void PassManager::registerFunctionPass(const std::string &id, FunctionPassCallback callback)
{
    auto cb = FunctionPassCallback(callback);
    passRegistry_[id] =
        detail::PassFactory{detail::PassKind::Function,
                            {},
                            [passId = std::string(id), cb]()
                            { return std::make_unique<LambdaFunctionPass>(passId, cb); }};
}

/// @brief Register a function pass that executes a simple mutator lambda.
/// @details Wraps the provided mutator @p fn in a callback that always reports
/// `PreservedAnalyses::none`, ensuring caches associated with each function are
/// cleared after the pass. Updates the pass registry entry for @p id.
/// @param id Unique function pass identifier.
/// @param fn Callable invoked for each function; assumed to invalidate all analyses.
void PassManager::registerFunctionPass(const std::string &id,
                                       const std::function<void(core::Function &)> &fn)
{
    registerFunctionPass(id,
                         [fn](core::Function &function, AnalysisManager &)
                         {
                             fn(function);
                             return PreservedAnalyses::none();
                         });
}

/// @brief Register a reusable ordered list of pass identifiers.
/// @details Moves @p pipeline into the `pipelines_` map under @p id, replacing
/// any existing sequence so callers can execute the updated ordering later.
/// @param id Pipeline identifier used for lookup when executing.
/// @param pipeline Sequence of pass identifiers to execute in order.
void PassManager::registerPipeline(const std::string &id, Pipeline pipeline)
{
    pipelines_[id] = std::move(pipeline);
}

/// @brief Retrieve a previously registered pipeline by identifier.
/// @details Searches `pipelines_` for @p id and returns a pointer to the stored
/// sequence so callers can execute or inspect it without copying.
/// @param id Pipeline identifier to search for.
/// @return Pointer to the stored pipeline or nullptr when not registered.
const PassManager::Pipeline *PassManager::getPipeline(const std::string &id) const
{
    auto it = pipelines_.find(id);
    return it == pipelines_.end() ? nullptr : &it->second;
}

/// @brief Enable or disable verifier checks executed between passes.
/// @details Mutates the `verifyBetweenPasses_` flag so debug builds either call
/// the IL verifier after each pass or skip the extra validation step.
/// @param enable Whether verification should run after each pass (debug only).
void PassManager::setVerifyBetweenPasses(bool enable)
{
    verifyBetweenPasses_ = enable;
}

/// @brief Execute the provided pipeline on the supplied module.
/// @details Constructs an AnalysisManager for @p module, iterates each
/// identifier in @p pipeline, instantiates the referenced pass, and executes it.
/// Module passes run once per pipeline, whereas function passes run once per
/// function. After each pass the reported preservation summary is fed back into
/// the analysis manager to drop invalidated caches, and debug builds optionally
/// rerun the IL verifier. Invoked passes may mutate the module and analysis
/// caches.
/// @param module Module to transform.
/// @param pipeline Ordered list of pass identifiers to execute.
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

/// @brief Execute a named pipeline if it has been registered.
/// @details Looks up @p pipelineId in the pipeline registry and delegates to
/// `run` when present. Leaves the module untouched and returns false if the
/// pipeline has not been registered.
/// @param module Module to transform.
/// @param pipelineId Identifier of the pipeline to execute.
/// @return True if the pipeline was found and executed; otherwise false.
bool PassManager::runPipeline(core::Module &module, const std::string &pipelineId) const
{
    const Pipeline *pipeline = getPipeline(pipelineId);
    if (!pipeline)
        return false;
    run(module, *pipeline);
    return true;
}

} // namespace il::transform
