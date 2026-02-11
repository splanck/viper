//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/SimplifyCFG/ReachabilityCleanup.hpp
// Purpose: Reachability-based cleanup for SimplifyCFG. Computes reachability
//          from the function entry block and removes unreachable blocks,
//          preserving EH structure. Updates branch terminators to drop labels
//          and argument bundles targeting removed blocks.
// Key invariants:
//   - EH-sensitive blocks are never removed even if unreachable.
//   - After cleanup, all remaining blocks are reachable from entry.
// Ownership/Lifetime: Stateless free function operating on caller-owned IR
//          via the SimplifyCFGPassContext reference.
// Links: il/transform/SimplifyCFG.hpp
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
