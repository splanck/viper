// File: src/il/transform/SimplifyCFG/ForwardingElimination.hpp
// License: MIT (see LICENSE for details).
// Purpose: Declares empty block forwarding elimination for SimplifyCFG.
// Key invariants: Redirects predecessors without altering live semantics.
// Ownership/Lifetime: Modifies caller-owned CFG blocks in place.
// Links: docs/codemap.md
#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

bool removeEmptyForwarders(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
