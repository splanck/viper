// File: src/il/transform/Peephole.hpp
// Purpose: Declares a peephole optimizer for IL modules.
// Key invariants: Applies local simplifications preserving semantics.
// Ownership/Lifetime: Operates in place on the provided module.
// Links: docs/class-catalog.md
#pragma once
#include "il/core/Module.hpp"

namespace il::transform
{

/// \brief Run peephole simplifications over @p m.
void peephole(core::Module &m);

} // namespace il::transform
