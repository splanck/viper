//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/ConstFold.hpp
// Purpose: Declares the constant folding pass -- replaces instructions whose
//          operands are all literal constants with the computed constant result.
//          Covers integer arithmetic, bitwise ops, comparisons, and selected
//          math intrinsics.
// Key invariants:
//   - Only folds when both operands are literals and the operation is safe.
//   - Mutates the module in place; no dataflow analysis is performed.
// Ownership/Lifetime: Free function operating on a caller-owned Module.
// Links: il/core/fwd.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"

namespace il::transform
{

/// \brief Fold trivial constant computations in @p m.
void constFold(core::Module &m);

} // namespace il::transform
