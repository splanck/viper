// File: src/il/transform/DCE.hpp
// Purpose: Dead-code elimination for IL.
// Key invariants: Removes unused temps and redundant memory ops.
// Ownership/Lifetime: Mutates the module in place.
// Links: docs/codemap.md
#pragma once

#include "il/core/fwd.hpp"

namespace il::transform
{

/// \brief Eliminate trivial dead code and unused block parameters.
/// \param M Module to simplify in place.
void dce(il::core::Module &M);

} // namespace il::transform
