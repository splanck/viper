// File: src/il/transform/PassManager.hpp
// Purpose: Declare a modular IL pass manager with analysis caching and pipelines.
// Key invariants: Pass registration IDs are unique; analysis caches respect preservation info.
// Ownership/Lifetime: PassManager stores factories and analysis descriptors with static lifetime.
// Links: docs/class-catalog.md
#pragma once

#include "il/core/fwd.hpp"

#include <any>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::transform
{

/// @brief Cached control-flow information for a function.
struct CFGInfo
{
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>> successors;
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>>
        predecessors;
};

/// @brief Cached liveness sets (live-in/live-out) for each block.
class LivenessInfo
{
  public:
    /// @brief Lightweight view over the live value bitset for a block edge.
    class SetView
    {
      public:
        SetView() = default;

        /// @brief Query whether a value identifier is marked live.
        /// @param valueId Dense identifier for the SSA value.
        /// @return True when the identifier is within range and flagged live.
        bool contains(unsigned valueId) const
        {
            return bits_ && valueId < bits_->size() && (*bits_)[valueId];
        }

        /// @brief Visit every live value identifier in the set.
        /// @tparam Fn Callable invoked with each live value identifier.
        template <typename Fn> void forEach(Fn &&fn) const
        {
            if (!bits_)
                return;
            for (unsigned id = 0; id < bits_->size(); ++id)
            {
                if ((*bits_)[id])
                    fn(id);
            }
        }

        /// @brief Check whether the set contains any live values.
        /// @return True when no value bits are set.
        bool empty() const
        {
            if (!bits_)
                return true;
            for (bool bit : *bits_)
            {
                if (bit)
                    return false;
            }
            return true;
        }

        /// @brief Access the underlying bitset for integration tests or debugging.
        /// @return Reference to the immutable bitset representation.
        const std::vector<bool> &bits() const
        {
            assert(bits_ && "liveness set view is empty");
            return *bits_;
        }

      private:
        explicit SetView(const std::vector<bool> *bits) : bits_(bits) {}

        const std::vector<bool> *bits_ = nullptr;

        friend class LivenessInfo;
    };

    /// @brief Retrieve the live-in set for @p block.
    /// @param block Basic block whose entry set is requested.
    /// @return Lightweight view over the live-in values.
    SetView liveIn(const core::BasicBlock &block) const
    {
        return liveIn(&block);
    }

    /// @brief Retrieve the live-in set for @p block.
    /// @param block Basic block pointer whose entry set is requested.
    /// @return Lightweight view over the live-in values.
    SetView liveIn(const core::BasicBlock *block) const
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
    SetView liveOut(const core::BasicBlock &block) const
    {
        return liveOut(&block);
    }

    /// @brief Retrieve the live-out set for @p block pointer.
    /// @param block Basic block pointer whose exit set is requested.
    /// @return Lightweight view over the live-out values.
    SetView liveOut(const core::BasicBlock *block) const
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
    std::size_t valueCount() const
    {
        return valueCount_;
    }

  private:
    using BitSet = std::vector<bool>;

    std::size_t valueCount_{0};
    std::unordered_map<const core::BasicBlock *, BitSet> liveInBits_;
    std::unordered_map<const core::BasicBlock *, BitSet> liveOutBits_;

    friend LivenessInfo computeLiveness(core::Module &module, core::Function &fn);
    friend LivenessInfo computeLiveness(core::Module &module,
                                        core::Function &fn,
                                        const CFGInfo &cfg);
};

LivenessInfo computeLiveness(core::Module &module, core::Function &fn);
LivenessInfo computeLiveness(core::Module &module, core::Function &fn, const CFGInfo &cfg);

/// @brief Tracks which analyses remain valid after a pass executes.
class PreservedAnalyses
{
  public:
    static PreservedAnalyses all();
    static PreservedAnalyses none();

    PreservedAnalyses &preserveModule(const std::string &id);
    PreservedAnalyses &preserveFunction(const std::string &id);
    PreservedAnalyses &preserveAllModules();
    PreservedAnalyses &preserveAllFunctions();

    bool preservesAllModuleAnalyses() const;
    bool preservesAllFunctionAnalyses() const;
    bool isModulePreserved(const std::string &id) const;
    bool isFunctionPreserved(const std::string &id) const;
    bool hasModulePreservations() const;
    bool hasFunctionPreservations() const;

  private:
    bool preserveAllModules_ = false;
    bool preserveAllFunctions_ = false;
    std::unordered_set<std::string> moduleAnalyses_;
    std::unordered_set<std::string> functionAnalyses_;
};

class AnalysisManager;

/// @brief Abstract base class for module-level passes.
class ModulePass
{
  public:
    virtual ~ModulePass() = default;
    virtual std::string_view id() const = 0;
    virtual PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) = 0;
};

/// @brief Abstract base class for function-level passes executed per function.
class FunctionPass
{
  public:
    virtual ~FunctionPass() = default;
    virtual std::string_view id() const = 0;
    virtual PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) = 0;
};

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

enum class PassKind
{
    Module,
    Function
};

struct PassFactory
{
    PassKind kind;
    std::function<std::unique_ptr<ModulePass>()> makeModule;
    std::function<std::unique_ptr<FunctionPass>()> makeFunction;
};
} // namespace detail

using ModuleAnalysisMap = std::unordered_map<std::string, detail::ModuleAnalysisRecord>;
using FunctionAnalysisMap = std::unordered_map<std::string, detail::FunctionAnalysisRecord>;

/// @brief Provides access to registered analyses with caching.
class AnalysisManager
{
  public:
    AnalysisManager(core::Module &module,
                    const ModuleAnalysisMap &moduleAnalyses,
                    const FunctionAnalysisMap &functionAnalyses);

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

  private:
    core::Module &module_;
    const ModuleAnalysisMap *moduleAnalyses_;
    const FunctionAnalysisMap *functionAnalyses_;
    std::unordered_map<std::string, std::any> moduleCache_;
    std::unordered_map<std::string, std::unordered_map<const core::Function *, std::any>>
        functionCache_;
};

/// @brief Coordinates pass execution, analysis caching, and pipelines.
class PassManager
{
  public:
    using Pipeline = std::vector<std::string>;
    using ModulePassFactory = std::function<std::unique_ptr<ModulePass>()>;
    using FunctionPassFactory = std::function<std::unique_ptr<FunctionPass>()>;
    using ModulePassCallback = std::function<PreservedAnalyses(core::Module &, AnalysisManager &)>;
    using FunctionPassCallback =
        std::function<PreservedAnalyses(core::Function &, AnalysisManager &)>;

    PassManager();

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

    void registerModulePass(const std::string &id, ModulePassFactory factory);
    void registerModulePass(const std::string &id, ModulePassCallback callback);
    void registerModulePass(const std::string &id, const std::function<void(core::Module &)> &fn);

    void registerFunctionPass(const std::string &id, FunctionPassFactory factory);
    void registerFunctionPass(const std::string &id, FunctionPassCallback callback);
    void registerFunctionPass(const std::string &id,
                              const std::function<void(core::Function &)> &fn);

    void registerPipeline(const std::string &id, Pipeline pipeline);
    const Pipeline *getPipeline(const std::string &id) const;

    void setVerifyBetweenPasses(bool enable);

    void run(core::Module &module, const Pipeline &pipeline) const;
    bool runPipeline(core::Module &module, const std::string &pipelineId) const;

  private:
    ModuleAnalysisMap moduleAnalyses_;
    FunctionAnalysisMap functionAnalyses_;
    std::unordered_map<std::string, detail::PassFactory> passRegistry_;
    std::unordered_map<std::string, Pipeline> pipelines_;
    bool verifyBetweenPasses_;
};

} // namespace il::transform
