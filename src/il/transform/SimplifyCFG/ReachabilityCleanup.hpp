//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Declares reachability-based cleanup for SimplifyCFG.
/// @details The entry point computes reachability from the function entry block
///          and removes blocks that are not visited, while preserving
///          exception-handling structure. It also updates branch terminators to
///          drop labels and argument bundles that referred to removed blocks.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

/// @brief Remove blocks that are unreachable from the entry block.
/// @details Performs a reachability traversal that follows branch, conditional
///          branch, switch, and resume edges, then erases blocks not marked
///          reachable (except EH-sensitive blocks). Before erasing a block the
///          helper removes any remaining label references and argument bundles
///          targeting it so the CFG stays internally consistent. Statistics and
///          optional debug logging are updated via the pass context.
/// @param ctx Pass context providing the function, EH checks, and stats sink.
/// @return True if any unreachable blocks were removed.
bool removeUnreachableBlocks(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
