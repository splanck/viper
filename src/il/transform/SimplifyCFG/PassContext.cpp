//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/SimplifyCFG/PassContext.cpp
// Purpose: Provide the out-of-line implementation for the SimplifyCFG pass
//          context that bundles diagnostics, logging, and statistics plumbing
//          shared by every block simplification routine.
// Key invariants: Debug logging must be cheap to query after construction and
//                 exception-handling sensitivity checks delegate to the common
//                 utility helpers so all transforms agree on the definition.
// Ownership/Lifetime: The context borrows the function, module, and statistics
//                     objects owned by the caller for the lifetime of the pass
//                     invocation.  No heap allocations or ownership transfers
//                     occur inside this translation unit.
// Links: docs/il-passes.md#simplifycfg
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the shared context used by the SimplifyCFG pass.
/// @details The context exposes logging helpers, EH sensitivity queries, and the
///          pass statistics aggregation required by the various transformation
///          modules.

#include "il/transform/SimplifyCFG.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include <cstdio>

namespace il::transform
{

/// @brief Build a pass context that exposes shared SimplifyCFG services.
///
/// @details The constructor simply records references to the caller-owned
///          function, optional module, and statistics accumulator so individual
///          transformations can report progress without depending on global
///          state.  The environment-derived debug flag is computed exactly once
///          here, ensuring later `isDebugLoggingEnabled()` checks avoid touching
///          the environment repeatedly.
///
/// @param function Function currently being simplified.
/// @param module Optional module owning @p function; enables verifier hooks.
/// @param stats Mutable statistics accumulator shared with the caller.
SimplifyCFG::SimplifyCFGPassContext::SimplifyCFGPassContext(il::core::Function &function,
                                                            const il::core::Module *module,
                                                            Stats &stats)
    : function(function), module(module), stats(stats),
      debugLoggingEnabled_(simplify_cfg::readDebugFlagFromEnv())
{
}

/// @brief Query whether debug logging is enabled for the pass.
///
/// @details The flag is cached during construction so this accessor amounts to
///          a single boolean load, keeping performance overhead negligible for
///          hot loops that need to guard trace statements.
///
/// @return @c true when debug logging should emit messages; otherwise @c false.
bool SimplifyCFG::SimplifyCFGPassContext::isDebugLoggingEnabled() const
{
    return debugLoggingEnabled_;
}

/// @brief Emit a debug log message when logging is enabled.
///
/// @details The helper first checks the cached flag and immediately returns
///          when logging is disabled.  Otherwise it prefixes the message with
///          the current function name and prints to @c stderr, matching the
///          diagnostics channel used throughout the optimizer.  The formatting
///          uses @c std::fprintf to avoid constructing temporary strings.
///
/// @param message Human-readable payload to print.
void SimplifyCFG::SimplifyCFGPassContext::logDebug(std::string_view message) const
{
    if (!isDebugLoggingEnabled())
        return;

    const char *name = function.name.c_str();
    std::fprintf(stderr, "[DEBUG][SimplifyCFG] %s: %.*s\n", name,
                 static_cast<int>(message.size()), message.data());
}

/// @brief Determine whether a block is sensitive to exception handling rules.
///
/// @details SimplifyCFG contains a single set of heuristics for deciding when a
///          block participates in exception edges (landing pads, unwind targets,
///          etc.).  The context forwards the query to the shared helper so that
///          every transformation observes the same notion of EH sensitivity and
///          no stale copies of the logic exist in client code.
///
/// @param block Block under inspection.
/// @return @c true when @p block must be preserved for EH correctness.
bool SimplifyCFG::SimplifyCFGPassContext::isEHSensitive(const il::core::BasicBlock &block) const
{
    return simplify_cfg::isEHSensitiveBlock(block);
}

} // namespace il::transform

