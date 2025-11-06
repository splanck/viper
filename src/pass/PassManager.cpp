//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/pass/PassManager.cpp
// Purpose: Define the shared pass manager façade used across IL and codegen.
// Key invariants: Pass registration remains idempotent and pipelines execute in
//                 the order provided by the caller while running optional
//                 instrumentation hooks.
// Ownership/Lifetime: Owns pass callbacks and instrumentation hooks by value;
//                     callers retain ownership of any captured state.
// Links: docs/architecture.md#passes and docs/codemap.md#pass-manager
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

/// @brief Register a pass implementation under a unique identifier.
/// @details Pass callbacks are stored by value so they remain valid for the
///          lifetime of the manager.  Re-registering an identifier replaces the
///          existing callback, allowing tests to override passes in isolation.
/// @param id Stable identifier used when building pipelines.
/// @param callback Callable that executes the pass and returns success status.
void PassManager::registerPass(std::string id, PassCallback callback)
{
    passes_[std::move(id)] = std::move(callback);
}

/// @brief Install instrumentation invoked before each pass executes.
/// @details The hook receives the pass identifier, enabling drivers to print the
///          IR or log progress.  Passing an empty callable clears the hook.
/// @param hook Callback invoked prior to every pass execution.
void PassManager::setPrintBeforeHook(PrintHook hook)
{
    printBefore_ = std::move(hook);
}

/// @brief Install instrumentation invoked after each pass successfully runs.
/// @details The hook fires only when the pass callback returns @c true,
///          matching the expectations of existing driver code that prints the IR
///          after transformations.  Assigning an empty callable disables the
///          instrumentation.
/// @param hook Callback invoked after each pass executes successfully.
void PassManager::setPrintAfterHook(PrintHook hook)
{
    printAfter_ = std::move(hook);
}

/// @brief Install a verifier hook that runs after each pass completes.
/// @details Verifier callbacks receive the pass identifier and should return
///          @c true when the IR remains valid.  Returning @c false terminates the
///          pipeline early, matching the behaviour of the historical manager.
/// @param hook Callable that verifies the IR after each pass.
void PassManager::setVerifyEachHook(VerifyHook hook)
{
    verifyEach_ = std::move(hook);
}

/// @brief Execute the passes referenced by @p pipeline in order.
/// @details For each identifier the manager invokes the print-before hook, the
///          registered pass, the optional verifier, and finally the print-after
///          hook.  Missing passes or failing callbacks stop the pipeline and
///          return @c false.  Successful completion yields @c true.
/// @param pipeline Ordered sequence of pass identifiers to execute.
/// @return @c true when every pass and verifier succeeds; otherwise @c false.
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

