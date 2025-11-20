//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// EarlyCSE (GVN-lite) — function-level pass
// Performs simple within-block common subexpression elimination for pure,
// side-effect-free instructions. Commutes operands for a small set of
// commutative ops to improve matching. Skips memory ops for now.
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
