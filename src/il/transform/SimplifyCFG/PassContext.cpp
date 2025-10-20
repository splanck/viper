//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements the shared context used by the SimplifyCFG pass.
/// @details The context exposes logging helpers, EH sensitivity queries, and the
/// pass statistics aggregation required by the various transformation modules.

#include "il/transform/SimplifyCFG.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include <cstdio>

namespace il::transform
{

/// @brief Build a pass context that exposes shared SimplifyCFG services.
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
/// @details Reads a cached environment-derived flag so that repeated checks are
/// cheap for hot code paths.
/// @return @c true when debug logging should emit messages; otherwise @c false.
bool SimplifyCFG::SimplifyCFGPassContext::isDebugLoggingEnabled() const
{
    return debugLoggingEnabled_;
}

/// @brief Emit a debug log message when logging is enabled.
/// @details Prefixes messages with the function name to aid correlation and
/// sends the formatted text to @c stderr. Calls become no-ops when logging is
/// disabled.
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
/// @details Defers to the shared helper from the SimplifyCFG utilities to avoid
/// duplicating the heuristics in every transformation.
/// @param block Block under inspection.
/// @return @c true when @p block must be preserved for EH correctness.
bool SimplifyCFG::SimplifyCFGPassContext::isEHSensitive(const il::core::BasicBlock &block) const
{
    return simplify_cfg::isEHSensitiveBlock(block);
}

} // namespace il::transform

