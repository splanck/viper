//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Declares parameter and branch-argument canonicalisation helpers.
/// @details The entry point rewrites block parameter lists to remove unused
///          entries and parameters that are identical across all predecessors,
///          then realigns branch argument lists so CFG edges remain arity
///          compatible. The routine mutates the function in place and relies on
///          the pass context to avoid EH-sensitive regions.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

/// @brief Canonicalise block parameters and incoming branch arguments.
/// @details For each non-EH-sensitive block, the helper first replaces
///          parameters that always receive the same value from every
///          predecessor by substituting that value within the block and
///          removing the corresponding arguments on incoming edges. It then
///          drops parameters that are never referenced in the block body and
///          trims predecessor argument lists to match the updated signature.
///          Statistics and debug logging are updated through the pass context,
///          and the function returns whether any canonicalisation occurred.
/// @param ctx Pass context containing the function under transformation and
///            mutable statistics.
/// @return True if any block signatures or edge arguments were simplified.
bool canonicalizeParamsAndArgs(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
