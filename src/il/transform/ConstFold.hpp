// File: src/il/transform/ConstFold.hpp
// Purpose: Declares IL constant folding transformations.
// Key invariants: Only folds operations with literal operands.
// Ownership/Lifetime: Mutates the module in place.
// Links: docs/class-catalog.md
#pragma once
#include "il/core/Module.hpp"

namespace il::transform
{

/// \brief Fold trivial constant computations in @p m.
void constFold(core::Module &m);

} // namespace il::transform
