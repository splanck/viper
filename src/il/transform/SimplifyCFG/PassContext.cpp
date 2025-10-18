//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Houses the reusable services shared by the SimplifyCFG transformation.  The
// pass context provides logging hooks, exception-sensitivity queries, and
// statistics aggregation so the individual simplification modules remain thin.
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
/// @details Caches references to the function under transformation, its
///          containing module, and the statistics accumulator supplied by the
///          caller.  The constructor also snapshots the debug-logging flag from
///          the environment so that subsequent queries become inexpensive.
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
/// @details Returns the cached result captured during construction rather than
///          re-reading the environment each time.  This keeps the predicate
///          usable in tight loops within the transformation.
/// @return @c true when debug logging should emit messages; otherwise @c false.
bool SimplifyCFG::SimplifyCFGPassContext::isDebugLoggingEnabled() const
{
    return debugLoggingEnabled_;
}

/// @brief Emit a debug log message when logging is enabled.
/// @details Guards the emission with @ref isDebugLoggingEnabled, prefixes the
///          payload with the current function name for context, and prints the
///          formatted line to @c stderr.  Messages are skipped entirely when the
///          debug flag is disabled so hot paths stay quiet.
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
/// @details Calls the shared utility from @c SimplifyCFG/Utils to centralise the
///          heuristics for recognising EH-sensitive blocks.  The indirection
///          keeps all transformations consistent about which blocks must be
///          preserved for correctness.
/// @param block Block under inspection.
/// @return @c true when @p block must be preserved for EH correctness.
bool SimplifyCFG::SimplifyCFGPassContext::isEHSensitive(const il::core::BasicBlock &block) const
{
    return simplify_cfg::isEHSensitiveBlock(block);
}

} // namespace il::transform

