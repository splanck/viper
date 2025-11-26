//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the pass registration infrastructure and preservation
// tracking mechanisms for the IL transformation pipeline. The pass registry
// maintains factories for creating pass instances and tracks which analyses
// remain valid after each transformation.
//
// Viper's optimization pipeline follows the LLVM pass manager design: passes
// are registered with unique identifiers, pipelines specify pass sequences,
// and preservation metadata enables intelligent caching of analysis results.
// The registry provides the foundation for extensible, modular optimization
// infrastructure.
//
// Key Components:
// - PreservedAnalyses: Communicates which analysis results remain valid after
//   a pass executes, enabling the pass manager to avoid redundant recomputation
// - PassRegistry: Maps pass names to factory functions, supporting dynamic
//   pipeline construction from textual pass specifications
// - Pass identity: Each pass has a unique identifier used for registration,
//   preservation queries, and diagnostic output
//
// Preservation Model:
// Passes return PreservedAnalyses objects indicating which analyses are still
// valid. The pass manager uses this information to invalidate cached results
// only when necessary. Passes can preserve all analyses, no analyses, or
// specific named analyses based on their transformation behavior.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/core/fwd.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace il::transform
{

class AnalysisManager;

/// @brief Tracks which analyses are preserved by a pass execution.
/// @details Used by passes to signal which analysis results remain valid
///          after transformation, enabling the pass manager to avoid
///          unnecessary recomputation.
class PreservedAnalyses
{
  public:
    /// @brief Create a PreservedAnalyses indicating all analyses are preserved.
    /// @return PreservedAnalyses object preserving all module and function analyses.
    static PreservedAnalyses all();

    /// @brief Create a PreservedAnalyses indicating no analyses are preserved.
    /// @return PreservedAnalyses object invalidating all analyses.
    static PreservedAnalyses none();

    /// @brief Mark a specific module-level analysis as preserved.
    /// @param id Analysis identifier to preserve.
    /// @return Reference to this object for method chaining.
    PreservedAnalyses &preserveModule(const std::string &id);

    /// @brief Mark a specific function-level analysis as preserved.
    /// @param id Analysis identifier to preserve.
    /// @return Reference to this object for method chaining.
    PreservedAnalyses &preserveFunction(const std::string &id);

    /// @brief Mark all module-level analyses as preserved.
    /// @return Reference to this object for method chaining.
    PreservedAnalyses &preserveAllModules();

    /// @brief Mark all function-level analyses as preserved.
    /// @return Reference to this object for method chaining.
    PreservedAnalyses &preserveAllFunctions();

    /// @brief Check if all module-level analyses are preserved.
    /// @return True if all module analyses remain valid.
    bool preservesAllModuleAnalyses() const;

    /// @brief Check if all function-level analyses are preserved.
    /// @return True if all function analyses remain valid.
    bool preservesAllFunctionAnalyses() const;

    /// @brief Check if a specific module analysis is preserved.
    /// @param id Analysis identifier to query.
    /// @return True if the module analysis remains valid.
    bool isModulePreserved(const std::string &id) const;

    /// @brief Check if a specific function analysis is preserved.
    /// @param id Analysis identifier to query.
    /// @return True if the function analysis remains valid.
    bool isFunctionPreserved(const std::string &id) const;

    /// @brief Check if any module analyses are explicitly preserved.
    /// @return True if specific module analyses or all module analyses are preserved.
    bool hasModulePreservations() const;

    /// @brief Check if any function analyses are explicitly preserved.
    /// @return True if specific function analyses or all function analyses are preserved.
    bool hasFunctionPreservations() const;

  private:
    bool preserveAllModules_ = false;
    bool preserveAllFunctions_ = false;
    std::unordered_set<std::string> moduleAnalyses_;
    std::unordered_set<std::string> functionAnalyses_;
};

/// @brief Base class for transformation passes operating on entire modules.
/// @details Module passes can modify any function, global, or extern declaration
///          within the module. They receive the full AnalysisManager for querying
///          cached analysis results.
class ModulePass
{
  public:
    virtual ~ModulePass() = default;

    /// @brief Get the unique identifier for this pass.
    /// @return String view representing the pass name.
    virtual std::string_view id() const = 0;

    /// @brief Execute the transformation on the module.
    /// @param module Module to transform.
    /// @param analysis Analysis manager for querying cached results.
    /// @return PreservedAnalyses indicating which analyses remain valid.
    virtual PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) = 0;
};

/// @brief Base class for transformation passes operating on individual functions.
/// @details Function passes transform one function at a time and can query
///          analyses at both function and module scope.
class FunctionPass
{
  public:
    virtual ~FunctionPass() = default;

    /// @brief Get the unique identifier for this pass.
    /// @return String view representing the pass name.
    virtual std::string_view id() const = 0;

    /// @brief Execute the transformation on a single function.
    /// @param function Function to transform.
    /// @param analysis Analysis manager for querying cached results.
    /// @return PreservedAnalyses indicating which analyses remain valid.
    virtual PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) = 0;
};

namespace detail
{
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

/// @brief Registry of available transformation passes for the IL optimizer.
/// @details Stores factories and callbacks for module and function passes,
///          enabling dynamic pass lookup and instantiation at pipeline
///          construction time.
class PassRegistry
{
  public:
    using ModulePassFactory = std::function<std::unique_ptr<ModulePass>()>;
    using FunctionPassFactory = std::function<std::unique_ptr<FunctionPass>()>;
    using ModulePassCallback = std::function<PreservedAnalyses(core::Module &, AnalysisManager &)>;
    using FunctionPassCallback =
        std::function<PreservedAnalyses(core::Function &, AnalysisManager &)>;

    /// @brief Register a module pass using a factory function.
    /// @param id Unique identifier for the pass.
    /// @param factory Function returning a new ModulePass instance.
    void registerModulePass(const std::string &id, ModulePassFactory factory);

    /// @brief Register a module pass using a callback with analysis access.
    /// @param id Unique identifier for the pass.
    /// @param callback Function implementing the pass transformation.
    void registerModulePass(const std::string &id, ModulePassCallback callback);

    /// @brief Register a simple module pass without analysis access.
    /// @param id Unique identifier for the pass.
    /// @param fn Function transforming the module (no return value).
    void registerModulePass(const std::string &id, const std::function<void(core::Module &)> &fn);

    /// @brief Register a function pass using a factory function.
    /// @param id Unique identifier for the pass.
    /// @param factory Function returning a new FunctionPass instance.
    void registerFunctionPass(const std::string &id, FunctionPassFactory factory);

    /// @brief Register a function pass using a callback with analysis access.
    /// @param id Unique identifier for the pass.
    /// @param callback Function implementing the pass transformation.
    void registerFunctionPass(const std::string &id, FunctionPassCallback callback);

    /// @brief Register a simple function pass without analysis access.
    /// @param id Unique identifier for the pass.
    /// @param fn Function transforming a function (no return value).
    void registerFunctionPass(const std::string &id,
                              const std::function<void(core::Function &)> &fn);

    /// @brief Look up a registered pass by identifier.
    /// @param id Pass identifier to query.
    /// @return Pointer to PassFactory if found, nullptr otherwise.
    const detail::PassFactory *lookup(std::string_view id) const;

  private:
    std::unordered_map<std::string, detail::PassFactory> registry_;
};

/// @brief Register the loop simplification pass with the registry.
/// @param registry PassRegistry to register the pass into.
void registerLoopSimplifyPass(PassRegistry &registry);

/// @brief Register the sparse conditional constant propagation pass.
/// @param registry PassRegistry to register the pass into.
void registerSCCPPass(PassRegistry &registry);

/// @brief Register the constant folding pass.
void registerConstFoldPass(PassRegistry &registry);

/// @brief Register the peephole/inst-combine style pass.
void registerPeepholePass(PassRegistry &registry);

/// @brief Register the trivial dead-code elimination pass.
void registerDCEPass(PassRegistry &registry);

/// @brief Register the mem2reg promotion pass.
void registerMem2RegPass(PassRegistry &registry);

/// @brief Register a simple dead-store elimination pass (placeholder).
void registerDSEPass(PassRegistry &registry);

/// @brief Register an EarlyCSE/GVN-lite pass (placeholder).
void registerEarlyCSEPass(PassRegistry &registry);

/// @brief Register the GVN + redundant load elimination pass.
void registerGVNPass(PassRegistry &registry);

/// @brief Register the IndVarSimplify pass.
void registerIndVarSimplifyPass(PassRegistry &registry);

/// @brief Register the simple function inliner module pass.
void registerInlinePass(PassRegistry &registry);

/// @brief Register the check optimization pass.
void registerCheckOptPass(PassRegistry &registry);

/// @brief Register the late cleanup pass.
void registerLateCleanupPass(PassRegistry &registry);

} // namespace il::transform
