// File: src/il/transform/ConstFold.hpp
// Purpose: Declares IL constant folding transformations.
// Key invariants: Only folds operations with literal operands.
// Ownership/Lifetime: Mutates the module in place.
// Notes: Includes folding of selected math intrinsics at the IL level.
// Links: docs/class-catalog.md
#pragma once

#include "il/core/Module.hpp"

namespace il::transform
{

/// \brief Fold trivial constant computations and math intrinsics in @p m.
/// \note Runs on the IL after parsing.
void constFold(core::Module &m);

} // namespace il::transform
