//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/pass/PassManager.cpp
// Purpose: Define the shared pass manager façade used across IL and codegen.
// Invariants: Registered passes remain valid until the manager is destroyed, and
//             instrumentation hooks are optional but, when present, are invoked
//             deterministically before/after each pass.
// Ownership/Lifetime: Pass callbacks are stored by value and executed in the
//                     order determined by the pipeline description.
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

/// @brief Register a pass callback under the supplied identifier.
/// @details Replaces any existing callback for @p id and stores the new
///          invokable in the manager's registry.  The callback is expected to
///          return @c true on success and @c false on fatal failure.
/// @param id Unique identifier used when constructing pipelines.
/// @param callback Functor invoked when the pass is executed.
void PassManager::registerPass(std::string id, PassCallback callback)
{
    passes_[std::move(id)] = std::move(callback);
}

/// @brief Install a hook that runs before each pass executes.
/// @details When provided, the hook receives the pass identifier so tooling can
///          print headers or perform instrumentation.  Passing an empty hook
///          clears the existing callback.
/// @param hook Functor invoked before pass execution.
void PassManager::setPrintBeforeHook(PrintHook hook)
{
    printBefore_ = std::move(hook);
}

/// @brief Install a hook that runs after each pass finishes.
/// @details Allows the manager to surface post-pass state such as IR dumps or
///          metrics.  Passing an empty hook removes any previously installed
///          callback.
/// @param hook Functor invoked after pass execution.
void PassManager::setPrintAfterHook(PrintHook hook)
{
    printAfter_ = std::move(hook);
}

/// @brief Install a verification hook executed after each pass.
/// @details When present, the hook receives the pass identifier and returns a
///          boolean signalling whether the IR remains valid.  A @c false return
///          aborts the pipeline immediately.
/// @param hook Verification functor or empty callable to disable verification.
void PassManager::setVerifyEachHook(VerifyHook hook)
{
    verifyEach_ = std::move(hook);
}

/// @brief Execute an ordered list of passes registered with the manager.
/// @details Looks up each pass identifier, invoking optional before/after hooks
///          around the pass callback.  Verification hooks run after the pass and
///          can abort the pipeline when they return @c false.  Missing pass
///          identifiers or callbacks that return failure terminate the pipeline
///          and propagate @c false to the caller.
/// @param pipeline Sequence of pass identifiers to execute.
/// @return @c true when every pass succeeds and verification (if enabled)
///         passes; @c false otherwise.
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

