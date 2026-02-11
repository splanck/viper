//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/DCE.hpp
// Purpose: Declares the dead code elimination pass -- removes instructions
//          and block parameters whose results are never used, using backward
//          dataflow liveness analysis. Preserves side-effectful operations.
// Key invariants:
//   - Instructions with side effects (stores, calls, terminators) are never
//     removed.
//   - SSA form and CFG integrity are maintained after elimination.
// Ownership/Lifetime: Free function operating on a caller-owned Module.
// Links: il/core/fwd.hpp
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
