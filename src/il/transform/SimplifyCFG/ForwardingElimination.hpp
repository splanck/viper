//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/SimplifyCFG/ForwardingElimination.hpp
// Purpose: Declares empty block forwarding elimination for SimplifyCFG.
// Key invariants: Redirects predecessors without altering live semantics.
// Ownership/Lifetime: Modifies caller-owned CFG blocks in place.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/SimplifyCFG.hpp"

namespace il::transform::simplify_cfg
{

bool removeEmptyForwarders(SimplifyCFG::SimplifyCFGPassContext &ctx);

} // namespace il::transform::simplify_cfg
