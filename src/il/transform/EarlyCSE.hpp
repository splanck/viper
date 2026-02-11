//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/EarlyCSE.hpp
// Purpose: EarlyCSE (GVN-lite) -- simple within-block common subexpression
//          elimination for pure, side-effect-free instructions. Commutes
//          operands for commutative ops to improve matching. Skips memory ops.
// Key invariants:
//   - Only eliminates instructions that are pure and non-trapping.
//   - Operates block-locally; no cross-block analysis.
// Ownership/Lifetime: Free function operating on a caller-owned Function.
// Links: il/core/Function.hpp, il/transform/ValueKey.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Function.hpp"

namespace il::transform
{

/// \brief Eliminate redundant pure instructions within each basic block.
/// \return True when any instruction was removed or simplified.
bool runEarlyCSE(il::core::Function &F);

} // namespace il::transform
