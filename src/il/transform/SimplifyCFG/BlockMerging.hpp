//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/SimplifyCFG/BlockMerging.hpp
// Purpose: Declares block merging helpers for SimplifyCFG. 
// Key invariants: Merges preserve terminator semantics and argument mapping.
// Ownership/Lifetime: Mutates predecessor/successor blocks owned by caller.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

bool mergeSinglePredBlocks(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
