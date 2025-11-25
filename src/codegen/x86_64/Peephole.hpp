//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Peephole.hpp
// Purpose: Declare peephole optimisations over the provisional Machine IR for
// Key invariants: To be documented.
// Ownership/Lifetime: Operates on mutable Machine IR owned by the caller; no new
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"

namespace viper::codegen::x64
{

/// \brief Run conservative Machine IR peepholes for Phase A bring-up.
void runPeepholes(MFunction &fn);

} // namespace viper::codegen::x64
