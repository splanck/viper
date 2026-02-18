//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the transform pass registry responsible for constructing pass
// instances on demand and tracking preservation summaries.  The registry
// decouples pass registration from pipeline execution so tools can lazily look
// up factories by identifier without hard-coding dependencies.
//
//===----------------------------------------------------------------------===//

#include "il/transform/PassRegistry.hpp"

#include "il/transform/AnalysisIDs.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/DSE.hpp"
#include "il/transform/EarlyCSE.hpp"
#include "il/transform/GVN.hpp"
#include "il/transform/IndVarSimplify.hpp"
#include "il/transform/Inline.hpp"
#include "il/transform/LICM.hpp"
#include "il/transform/LoopSimplify.hpp"
#include "il/transform/Mem2Reg.hpp"
#include "il/transform/Peephole.hpp"
#include "il/transform/SCCP.hpp"

#include <utility>

namespace il::transform
{

/// @brief Describe a summary where every registered analysis remains valid.
/// @details Returns an instance that marks both module and function analyses as
///          fully preserved, allowing the pipeline executor to skip invalidation
///          entirely because no cached data has to be recomputed.
/// @return Preservation summary retaining all analyses.
PreservedAnalyses PreservedAnalyses::all()
{
    PreservedAnalyses p;
    p.preserveAllModules_ = true;
    p.preserveAllFunctions_ = true;
    return p;
}

/// @brief Produce a summary indicating that no analyses remain valid.
/// @details Leaves the module/function preservation flags unset and the
///          preserved sets empty so the executor will purge all cached analyses
///          on the next invalidation pass.
/// @return Preservation summary invalidating every analysis.
PreservedAnalyses PreservedAnalyses::none()
{
    return PreservedAnalyses{};
}

/// @brief Record that a specific module analysis was preserved by a pass.
/// @details Inserts the identifier into the preserved set so the invalidator can
///          recognise that the cached result remains valid after the pass
///          finishes executing.
/// @param id Identifier of the module analysis to keep.
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveModule(const std::string &id)
{
    moduleAnalyses_.insert(id);
    return *this;
}

/// @brief Record that a specific function analysis was preserved by a pass.
/// @details Mirrors @ref preserveModule by marking a function analysis as
///          retained, enabling selective invalidation when only certain analyses
///          become stale.
/// @param id Identifier of the function analysis to keep.
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveFunction(const std::string &id)
{
    functionAnalyses_.insert(id);
    return *this;
}

/// @brief Mark every registered module analysis as preserved.
/// @details Sets the fast-path flag that prevents the invalidator from scanning
///          individual module analysis entries, providing an efficient escape
///          hatch for passes that leave the entire module analysis cache intact.
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveAllModules()
{
    preserveAllModules_ = true;
    return *this;
}

/// @brief Mark every registered function analysis as preserved.
/// @details Enables a shortcut similar to @ref preserveAllModules so the
///          invalidator can skip per-analysis checks for function-scoped
///          results.
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveAllFunctions()
{
    preserveAllFunctions_ = true;
    return *this;
}

/// @brief Check whether all module analyses were preserved.
/// @details Reports whether the fast-path flag set by
///          @ref preserveAllModules() is active, allowing callers to avoid set
///          lookups when the entire cache remains valid.
/// @return @c true when @ref PreservedAnalyses::preserveAllModules was invoked.
bool PreservedAnalyses::preservesAllModuleAnalyses() const
{
    return preserveAllModules_;
}

/// @brief Check whether all function analyses were preserved.
/// @details Reports whether the fast-path flag set by
///          @ref preserveAllFunctions() is active.
/// @return @c true when @ref PreservedAnalyses::preserveAllFunctions was invoked.
bool PreservedAnalyses::preservesAllFunctionAnalyses() const
{
    return preserveAllFunctions_;
}

/// @brief Determine whether a specific module analysis is preserved.
/// @details Checks the fast-path flag and falls back to the preserved identifier
///          set, enabling selective retention of cached results.
/// @param id Analysis identifier to query.
/// @return @c true when the analysis was explicitly preserved or the summary
///         preserves all module analyses.
bool PreservedAnalyses::isModulePreserved(const std::string &id) const
{
    return preserveAllModules_ || moduleAnalyses_.contains(id);
}

/// @brief Determine whether a specific function analysis is preserved.
/// @details Mirrors @ref isModulePreserved by consulting the function
///          preservation data.
/// @param id Analysis identifier to query.
/// @return @c true when the analysis was explicitly preserved or the summary
///         preserves all function analyses.
bool PreservedAnalyses::isFunctionPreserved(const std::string &id) const
{
    return preserveAllFunctions_ || functionAnalyses_.contains(id);
}

/// @brief Check whether any module analyses were explicitly preserved.
/// @details Allows callers to distinguish between "preserve everything" and
///          "preserve only these identifiers" cases when invalidating caches.
/// @return @c true when the module preservation set is non-empty.
bool PreservedAnalyses::hasModulePreservations() const
{
    return !moduleAnalyses_.empty();
}

/// @brief Check whether any function analyses were explicitly preserved.
/// @details Companion to @ref hasModulePreservations for the function-level
///          cache.
/// @return @c true when the function preservation set is non-empty.
bool PreservedAnalyses::hasFunctionPreservations() const
{
    return !functionAnalyses_.empty();
}

PreservedAnalyses &PreservedAnalyses::preserveCFG()
{
    return preserveFunction(kAnalysisCFG);
}

PreservedAnalyses &PreservedAnalyses::preserveDominators()
{
    return preserveFunction(kAnalysisDominators);
}

PreservedAnalyses &PreservedAnalyses::preserveLoopInfo()
{
    return preserveFunction(kAnalysisLoopInfo);
}

PreservedAnalyses &PreservedAnalyses::preserveLiveness()
{
    return preserveFunction(kAnalysisLiveness);
}

PreservedAnalyses &PreservedAnalyses::preserveBasicAA()
{
    return preserveFunction(kAnalysisBasicAA);
}

namespace
{
class LambdaModulePass : public ModulePass
{
  public:
    /// @brief Wrap a module-pass callback with the @ref ModulePass interface.
    /// @details Stores the identifier for reporting and the callback used to
    ///          implement the pass.  Instances are created on demand by the
    ///          registry when a pass is requested so pipelines can remain
    ///          decoupled from concrete types.
    /// @param id Identifier supplied during registration.
    /// @param cb Callback invoked when the pass executes.
    LambdaModulePass(std::string id, PassRegistry::ModulePassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    /// @brief Expose the identifier under which the pass was registered.
    /// @details Allows the executor to report which pass is currently running
    ///          during diagnostics or verification steps.
    /// @return String view referencing the stored identifier.
    std::string_view id() const override
    {
        return id_;
    }

    /// @brief Execute the wrapped module pass callback.
    /// @details Simply forwards to the stored callback and returns the
    ///          preservation summary so the executor can invalidate analyses.
    /// @param module Module being transformed.
    /// @param analysis Analysis manager providing cached queries.
    /// @return Preservation summary reported by the callback.
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
    /// @brief Wrap a function-pass callback with the @ref FunctionPass interface.
    /// @details Stores the identifier and callback so lookups yield full
    ///          @ref FunctionPass instances without exposing concrete pass types.
    /// @param id Identifier supplied during registration.
    /// @param cb Callback invoked when the pass executes.
    LambdaFunctionPass(std::string id, PassRegistry::FunctionPassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    /// @brief Expose the identifier under which the pass was registered.
    /// @details Mirrors the module variant for diagnostic reporting.
    /// @return String view referencing the stored identifier.
    std::string_view id() const override
    {
        return id_;
    }

    /// @brief Execute the wrapped function pass callback.
    /// @details Forwards to the stored callback and returns the resulting
    ///          preservation summary for downstream invalidation.
    /// @param function Function being transformed.
    /// @param analysis Analysis manager providing cached queries.
    /// @return Preservation summary reported by the callback.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override
    {
        return callback_(function, analysis);
    }

  private:
    std::string id_;
    PassRegistry::FunctionPassCallback callback_;
};
} // namespace

/// @brief Register a module pass factory under a stable identifier.
/// @details Stores the factory inside the registry so future lookups can
///          synthesize fresh pass instances on demand.  Ownership of the factory
///          is transferred to the registry, ensuring it remains valid for the
///          program lifetime.
/// @param id Identifier used to reference the pass from pipelines.
/// @param factory Callable producing unique @ref ModulePass instances.
void PassRegistry::registerModulePass(const std::string &id, ModulePassFactory factory)
{
    registry_[id] = detail::PassFactory{detail::PassKind::Module, std::move(factory), {}};
}

/// @brief Register a module pass implemented via a simple callback.
/// @details Wraps the callback in a lambda-backed @ref ModulePass so the
///          registry can supply polymorphic instances to the executor while
///          keeping registration sites terse.
/// @param id Identifier used to reference the pass from pipelines.
/// @param callback Callback implementing the pass behaviour.
void PassRegistry::registerModulePass(const std::string &id, ModulePassCallback callback)
{
    auto cb = ModulePassCallback(callback);
    registry_[id] = detail::PassFactory{detail::PassKind::Module,
                                        [passId = std::string(id), cb]()
                                        { return std::make_unique<LambdaModulePass>(passId, cb); },
                                        {}};
}

/// @brief Register a void callback as a module pass.
/// @details Convenience overload that upgrades a basic callback into a pass
///          returning @ref PreservedAnalyses::none(), allowing quick-and-dirty
///          passes to participate in the framework.
/// @param id Identifier used to reference the pass from pipelines.
/// @param fn Callback executed when the pass runs.
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

/// @brief Register a function pass factory under a stable identifier.
/// @details Transfers ownership of the factory callable so the registry can
///          instantiate fresh function passes whenever a pipeline references the
///          identifier.
/// @param id Identifier used to reference the pass from pipelines.
/// @param factory Callable producing unique @ref FunctionPass instances.
void PassRegistry::registerFunctionPass(const std::string &id, FunctionPassFactory factory)
{
    registry_[id] = detail::PassFactory{detail::PassKind::Function, {}, std::move(factory)};
}

/// @brief Register a function pass implemented via a simple callback.
/// @details Wraps the callback in a lambda-backed @ref FunctionPass similar to
///          the module overload so pipelines can work with opaque polymorphic
///          objects.
/// @param id Identifier used to reference the pass from pipelines.
/// @param callback Callback implementing the pass behaviour.
void PassRegistry::registerFunctionPass(const std::string &id, FunctionPassCallback callback)
{
    auto cb = FunctionPassCallback(callback);
    registry_[id] =
        detail::PassFactory{detail::PassKind::Function,
                            {},
                            [passId = std::string(id), cb]()
                            { return std::make_unique<LambdaFunctionPass>(passId, cb); }};
}

/// @brief Register a void callback as a function pass.
/// @details Wraps the callback in a preserving adaptor returning
///          @ref PreservedAnalyses::none() so simple lambdas can participate in
///          the pipeline infrastructure.
/// @param id Identifier used to reference the pass from pipelines.
/// @param fn Callback executed when the pass runs.
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

/// @brief Retrieve the factory metadata associated with an identifier.
/// @details Performs a lookup inside the registry and returns a pointer to the
///          stored factory record when found so executors can instantiate the
///          pass.
/// @param id Identifier previously supplied to a registration call.
/// @return Pointer to the stored factory or @c nullptr when the identifier is
///         unknown.
const detail::PassFactory *PassRegistry::lookup(std::string_view id) const
{
    auto it = registry_.find(std::string(id));
    if (it == registry_.end())
        return nullptr;
    return &it->second;
}

void registerLoopSimplifyPass(PassRegistry &registry)
{
    registry.registerFunctionPass("loop-simplify",
                                  []() { return std::make_unique<LoopSimplify>(); });
}

void registerLICMPass(PassRegistry &registry)
{
    registry.registerFunctionPass("licm", []() { return std::make_unique<LICM>(); });
}

void registerSCCPPass(PassRegistry &registry)
{
    registry.registerModulePass("sccp",
                                [](core::Module &module, AnalysisManager &)
                                {
                                    sccp(module);
                                    return PreservedAnalyses::none();
                                });
}

void registerConstFoldPass(PassRegistry &registry)
{
    registry.registerModulePass("constfold",
                                [](core::Module &module, AnalysisManager &)
                                {
                                    constFold(module);
                                    return PreservedAnalyses::none();
                                });
}

void registerPeepholePass(PassRegistry &registry)
{
    registry.registerModulePass("peephole",
                                [](core::Module &module, AnalysisManager &)
                                {
                                    peephole(module);
                                    return PreservedAnalyses::none();
                                });
}

void registerDCEPass(PassRegistry &registry)
{
    registry.registerModulePass("dce",
                                [](core::Module &module, AnalysisManager &)
                                {
                                    dce(module);
                                    return PreservedAnalyses::none();
                                });
}

void registerMem2RegPass(PassRegistry &registry)
{
    registry.registerModulePass("mem2reg",
                                [](core::Module &module, AnalysisManager &)
                                {
                                    viper::passes::mem2reg(module, nullptr);
                                    return PreservedAnalyses::none();
                                });
}

void registerDSEPass(PassRegistry &registry)
{
    registry.registerFunctionPass(
        "dse",
        [](core::Function &fn, AnalysisManager &am)
        {
            bool changed = runDSE(fn, am);
            // MemorySSA-based cross-block DSE: catches stores that
            // runDSE's conservative call-barrier logic would miss for
            // non-escaping allocas.
            changed |= runMemorySSADSE(fn, am);
            if (!changed)
                return PreservedAnalyses::all();
            PreservedAnalyses p; // conservatively invalidate function analyses
            p.preserveAllModules();
            return p;
        });
}

void registerEarlyCSEPass(PassRegistry &registry)
{
    registry.registerFunctionPass("earlycse",
                                  [](core::Function &fn, AnalysisManager &am)
                                  {
                                      bool changed = runEarlyCSE(am.module(), fn);
                                      if (!changed)
                                          return PreservedAnalyses::all();
                                      PreservedAnalyses p;
                                      p.preserveAllModules();
                                      return p;
                                  });
}

} // namespace il::transform
