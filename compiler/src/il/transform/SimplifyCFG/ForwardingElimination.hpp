//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Declares empty forwarding-block elimination for SimplifyCFG.
/// @details The entry point identifies blocks that only forward control and
///          arguments to a single successor, rewrites predecessor terminators to
///          target the successor directly, and removes the redundant blocks
///          once all edges are retargeted. The transformation preserves
///          exception-handling constraints and control-flow semantics.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

/// @brief Remove trivial forwarding blocks from the current function.
/// @details Scans for blocks that are safe to remove (single unconditional
///          branch, no side effects, no reuse of locally defined temporaries),
///          redirects all predecessor edges to the forwarding block's successor
///          with substituted argument values, and erases the block when no
///          remaining edges refer to it. Updates statistics and optional debug
///          logs through the pass context to report redirected edges and removed
///          blocks.
/// @param ctx Pass context containing the function under transformation and
///            statistics/logging utilities.
/// @return True if any edges were redirected or blocks removed.
bool removeEmptyForwarders(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
