//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/il/transform/SimplifyCFG/PassContext.cpp
// Purpose: Provide the shared context object consumed by SimplifyCFG transform steps.
// Key invariants: Debug logging flag is cached per-context; EH sensitivity checks
//                 defer to canonical utilities to keep behaviour consistent.
// Ownership/Lifetime: The context borrows function/module references supplied by
//                     the pass driver and updates caller-owned statistics.
// Links: docs/il-guide.md#reference, docs/codemap.md#il-transform
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
/// @details The constructor caches references to the current function, the
///          optional parent module, and the statistics struct supplied by the
///          driver.  It also snapshots the debug logging flag by consulting the
///          environment via @ref simplify_cfg::readDebugFlagFromEnv so that later
///          queries avoid repeated environment lookups.
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
/// @details Reads the cached boolean recorded at construction time.  The flag
///          is immutable for the lifetime of the context so hot paths can call
///          this accessor without incurring synchronisation or environment
///          lookups.
///
/// @return @c true when debug logging should emit messages; otherwise @c false.
bool SimplifyCFG::SimplifyCFGPassContext::isDebugLoggingEnabled() const
{
    return debugLoggingEnabled_;
}

/// @brief Emit a debug log message when logging is enabled.
///
/// @details Guards logging behind @ref isDebugLoggingEnabled and, when active,
///          prepends the owning function's name before writing the message to
///          @c stderr.  Using @c std::fprintf keeps the implementation
///          lightweight and avoids allocating temporary streams on hot paths.
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
/// @details Delegates to the utility function shared across SimplifyCFG so that
///          all transformations agree on the heuristics for preserving EH
///          structure.  This keeps the context free from policy details while
///          still offering a convenient access point.
///
/// @param block Block under inspection.
/// @return @c true when @p block must be preserved for EH correctness.
bool SimplifyCFG::SimplifyCFGPassContext::isEHSensitive(const il::core::BasicBlock &block) const
{
    return simplify_cfg::isEHSensitiveBlock(block);
}

} // namespace il::transform

