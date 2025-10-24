// File: src/il/transform/SimplifyCFG/ReachabilityCleanup.hpp
// License: MIT (see LICENSE for details).
// Purpose: Declares reachability-based cleanup for SimplifyCFG.
// Key invariants: Removes only blocks proven unreachable from entry.
// Ownership/Lifetime: Mutates the caller-owned function in place.
// Links: docs/codemap.md
#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

bool removeUnreachableBlocks(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
