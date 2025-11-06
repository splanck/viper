//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/pass/PassManager.cpp
// Purpose: Define the shared pass manager façade used across IL and codegen.
// Links: docs/architecture.md#passes
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the instrumentation-friendly pass manager façade.
/// @details Provides the plumbing to register pass callbacks, configure
///          instrumentation hooks, and execute ordered pipelines while
///          propagating success/failure information back to callers.

#include "viper/pass/PassManager.hpp"

#include <utility>

namespace viper::pass
{

/// @brief Register a pass implementation by identifier.
/// @details Associates @p id with the supplied callback in the manager's internal
///          map.  Subsequent pipeline executions resolve pass identifiers
///          through this table.  Existing entries are replaced, which allows
///          callers to inject alternate implementations during testing.
/// @param id Stable identifier for the pass (for example, "il.simplify").
/// @param callback Callable that executes the pass and returns success status.
void PassManager::registerPass(std::string id, PassCallback callback)
{
    passes_[std::move(id)] = std::move(callback);
}

/// @brief Install a hook that runs before each pass executes.
/// @details Hooks commonly print pass names or emit snapshots of the IR before
///          transformation.  Passing an empty @p hook clears the existing
///          callback, which tools can use to disable instrumentation at runtime.
/// @param hook Callable that receives the pass identifier about to run.
void PassManager::setPrintBeforeHook(PrintHook hook)
{
    printBefore_ = std::move(hook);
}

/// @brief Install a hook that runs after each pass completes.
/// @details Similar to @ref setPrintBeforeHook, this allows tooling to inspect
///          the program after every transformation.  Hooks may be cleared by
///          passing an empty callable.
/// @param hook Callable invoked once a pass finishes execution.
void PassManager::setPrintAfterHook(PrintHook hook)
{
    printAfter_ = std::move(hook);
}

/// @brief Install a verification hook that runs after each pass.
/// @details When present, the hook is called after every successful pass to
///          validate invariants (for example, IR well-formedness).  Returning
///          @c false aborts pipeline execution and surfaces the failure to
///          callers.
/// @param hook Callable invoked with the pass identifier to verify.
void PassManager::setVerifyEachHook(VerifyHook hook)
{
    verifyEach_ = std::move(hook);
}

/// @brief Execute the ordered list of passes described by @p pipeline.
/// @details Iterates the pipeline, invoking optional instrumentation hooks
///          before and after each pass.  If a pass identifier is unknown, the
///          pass returns @c false, or verification fails, the pipeline aborts and
///          the function reports failure.  The manager keeps no persistent state
///          beyond hook callbacks, so pipelines can be rerun safely.
/// @param pipeline Ordered identifiers describing the passes to execute.
/// @return @c true when the entire pipeline succeeds; otherwise @c false.
bool PassManager::runPipeline(const Pipeline &pipeline) const
{
    for (const auto &passId : pipeline)
    {
        if (printBefore_)
            printBefore_(passId);

        auto it = passes_.find(passId);
        if (it == passes_.end())
            return false;

        if (!it->second())
            return false;

        if (verifyEach_ && !verifyEach_(passId))
            return false;

        if (printAfter_)
            printAfter_(passId);
    }
    return true;
}

} // namespace viper::pass

