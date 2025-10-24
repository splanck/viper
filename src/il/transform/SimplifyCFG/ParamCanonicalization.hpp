// File: src/il/transform/SimplifyCFG/ParamCanonicalization.hpp
// License: MIT (see LICENSE for details).
// Purpose: Declares parameter canonicalisation routines for SimplifyCFG.
// Key invariants: Maintains block argument alignment across predecessors.
// Ownership/Lifetime: Operates on mutable blocks owned by the caller.
// Links: docs/codemap.md
#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

bool canonicalizeParamsAndArgs(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
