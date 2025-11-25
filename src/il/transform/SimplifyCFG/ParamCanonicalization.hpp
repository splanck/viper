//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/SimplifyCFG/ParamCanonicalization.hpp
// Purpose: Declares parameter canonicalisation routines for SimplifyCFG.
// Key invariants: Maintains block argument alignment across predecessors.
// Ownership/Lifetime: Operates on mutable blocks owned by the caller.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

bool canonicalizeParamsAndArgs(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
