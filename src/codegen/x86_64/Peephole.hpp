//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Peephole.hpp
// Purpose: Declare peephole optimisations over the provisional Machine IR.
// Key invariants: Rewrites preserve instruction ordering and semantics; only
//                 pattern substitutions (e.g., redundant moves, strength
//                 reduction) are applied without changing control flow.
// Ownership/Lifetime: Operates on mutable Machine IR owned by the caller; no
//                     dynamic resources are allocated beyond temporary vectors.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"

namespace viper::codegen::x64
{

/// \brief Run conservative Machine IR peepholes for Phase A bring-up.
/// \return Total number of transformations applied across all passes.
std::size_t runPeepholes(MFunction &fn);

} // namespace viper::codegen::x64
