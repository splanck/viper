//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

#include "il/transform/AnalysisManager.hpp"

#include <utility>

namespace il::transform
{

/// @brief Describe a summary where every registered analysis remains valid.
///
/// Returns an instance that marks both module and function analyses as fully
/// preserved, allowing the pipeline executor to skip invalidation entirely.
///
/// @return Preservation summary retaining all analyses.
PreservedAnalyses PreservedAnalyses::all()
{
    PreservedAnalyses p;
    p.preserveAllModules_ = true;
    p.preserveAllFunctions_ = true;
    return p;
}

/// @brief Produce a summary indicating that no analyses remain valid.
///
/// The returned summary leaves the module/function preservation flags unset and
/// the preserved sets empty so the executor will purge all caches.
///
/// @return Preservation summary invalidating every analysis.
PreservedAnalyses PreservedAnalyses::none()
{
    return PreservedAnalyses{};
}

/// @brief Record that a specific module analysis was preserved by a pass.
///
/// @param id Identifier of the module analysis to keep.
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveModule(const std::string &id)
{
    moduleAnalyses_.insert(id);
    return *this;
}

/// @brief Record that a specific function analysis was preserved by a pass.
///
/// @param id Identifier of the function analysis to keep.
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveFunction(const std::string &id)
{
    functionAnalyses_.insert(id);
    return *this;
}

/// @brief Mark every registered module analysis as preserved.
///
/// Sets the fast-path flag that prevents the invalidator from scanning
/// individual module analysis entries.
///
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveAllModules()
{
    preserveAllModules_ = true;
    return *this;
}

/// @brief Mark every registered function analysis as preserved.
///
/// Sets the fast-path flag that prevents the invalidator from scanning
/// individual function analysis entries.
///
/// @return Reference to @c *this for fluent chaining.
PreservedAnalyses &PreservedAnalyses::preserveAllFunctions()
{
    preserveAllFunctions_ = true;
    return *this;
}

/// @brief Check whether all module analyses were preserved.
///
/// @return @c true when @ref PreservedAnalyses::preserveAllModules was invoked.
bool PreservedAnalyses::preservesAllModuleAnalyses() const
{
    return preserveAllModules_;
}

/// @brief Check whether all function analyses were preserved.
///
/// @return @c true when @ref PreservedAnalyses::preserveAllFunctions was invoked.
bool PreservedAnalyses::preservesAllFunctionAnalyses() const
{
    return preserveAllFunctions_;
}

/// @brief Determine whether a specific module analysis is preserved.
///
/// @param id Analysis identifier to query.
/// @return @c true when the analysis was explicitly preserved or the summary
///         preserves all module analyses.
bool PreservedAnalyses::isModulePreserved(const std::string &id) const
{
    return preserveAllModules_ || moduleAnalyses_.count(id) > 0;
}

/// @brief Determine whether a specific function analysis is preserved.
///
/// @param id Analysis identifier to query.
/// @return @c true when the analysis was explicitly preserved or the summary
///         preserves all function analyses.
bool PreservedAnalyses::isFunctionPreserved(const std::string &id) const
{
    return preserveAllFunctions_ || functionAnalyses_.count(id) > 0;
}

/// @brief Check whether any module analyses were explicitly preserved.
///
/// @return @c true when the module preservation set is non-empty.
bool PreservedAnalyses::hasModulePreservations() const
{
    return !moduleAnalyses_.empty();
}

/// @brief Check whether any function analyses were explicitly preserved.
///
/// @return @c true when the function preservation set is non-empty.
bool PreservedAnalyses::hasFunctionPreservations() const
{
    return !functionAnalyses_.empty();
}

namespace
{
class LambdaModulePass : public ModulePass
{
  public:
    /// @brief Wrap a module-pass callback with the @ref ModulePass interface.
    ///
    /// Stores the identifier for reporting and the callback used to implement
    /// the pass.  Instances are created on demand by the registry when a pass is
    /// requested.
    ///
    /// @param id Identifier supplied during registration.
    /// @param cb Callback invoked when the pass executes.
    LambdaModulePass(std::string id, PassRegistry::ModulePassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    /// @brief Expose the identifier under which the pass was registered.
    ///
    /// @return String view referencing the stored identifier.
    std::string_view id() const override
    {
        return id_;
    }

    /// @brief Execute the wrapped module pass callback.
    ///
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
    ///
    /// @param id Identifier supplied during registration.
    /// @param cb Callback invoked when the pass executes.
    LambdaFunctionPass(std::string id, PassRegistry::FunctionPassCallback cb)
        : id_(std::move(id)), callback_(std::move(cb))
    {
    }

    /// @brief Expose the identifier under which the pass was registered.
    ///
    /// @return String view referencing the stored identifier.
    std::string_view id() const override
    {
        return id_;
    }

    /// @brief Execute the wrapped function pass callback.
    ///
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
///
/// Factories are stored by value so they remain valid for the lifetime of the
/// registry.  The caller must ensure that constructing a pass via the factory
/// yields a fresh instance on each invocation.
///
/// @param id Identifier used to reference the pass from pipelines.
/// @param factory Callable producing unique @ref ModulePass instances.
void PassRegistry::registerModulePass(const std::string &id, ModulePassFactory factory)
{
    registry_[id] = detail::PassFactory{detail::PassKind::Module, std::move(factory), {}};
}

/// @brief Register a module pass implemented via a simple callback.
///
/// Wraps the callback in a lambda-backed @ref ModulePass so the registry can
/// supply polymorphic instances to the executor.
///
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
///
/// Convenience overload that upgrades a basic callback into a pass returning
/// @ref PreservedAnalyses::none.
///
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
///
/// @param id Identifier used to reference the pass from pipelines.
/// @param factory Callable producing unique @ref FunctionPass instances.
void PassRegistry::registerFunctionPass(const std::string &id, FunctionPassFactory factory)
{
    registry_[id] = detail::PassFactory{detail::PassKind::Function, {}, std::move(factory)};
}

/// @brief Register a function pass implemented via a simple callback.
///
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
///
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
///
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

} // namespace il::transform
