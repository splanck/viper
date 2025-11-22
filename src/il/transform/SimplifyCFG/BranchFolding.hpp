//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/SimplifyCFG/BranchFolding.hpp
// Purpose: Declares branch folding routines for the SimplifyCFG pass. 
// Key invariants: Transformations preserve control flow equivalence.
// Ownership/Lifetime: Operates on caller-owned functions and blocks.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

bool foldTrivialSwitches(SimplifyCFG::SimplifyCFGPassContext &ctx);
bool foldTrivialConditionalBranches(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
