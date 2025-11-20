//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the dead code elimination (DCE) pass for IL modules. DCE
// removes instructions and block parameters that compute values never used by
// subsequent instructions, reducing code size and exposing further optimization
// opportunities.
//
// In SSA form, many optimizations introduce temporary values that become unused
// after subsequent transformations. For example, constant propagation may replace
// all uses of a temporary with a literal, leaving the defining instruction dead.
// DCE performs liveness analysis to identify and remove such instructions,
// keeping only those with observable effects (memory writes, calls, control flow).
//
// Key Responsibilities:
// - Remove instructions whose results are never used (no live uses)
// - Preserve instructions with side effects (stores, calls, intrinsics)
// - Eliminate unused basic block parameters when safe
// - Maintain SSA form and control flow graph integrity
//
// Design Notes:
// The pass uses backward dataflow analysis, starting from instructions with
// observable effects and marking their operands as live. Any instruction not
// marked live is dead and can be removed. The implementation is conservative,
// preserving all potentially effectful operations to maintain program semantics.
// Block parameters are eliminated only when no predecessor passes arguments to
// them and their values are unused within the block.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"

namespace il::transform
{

/// \brief Eliminate trivial dead code and unused block parameters.
/// \param M Module to simplify in place.
void dce(il::core::Module &M);

} // namespace il::transform
