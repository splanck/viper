// src/codegen/x86_64/Peephole.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare peephole optimisations over the provisional Machine IR for
//          the x86-64 backend.
// Invariants: Transformations preserve instruction operand invariants and only
//             match exact instruction forms deemed safe for Phase A.
// Ownership: Operates on mutable Machine IR owned by the caller; no new
//            allocations escape this interface.
// Notes: Keep the peepholes conservative until broader coverage is required.

#pragma once

#include "MachineIR.hpp"

namespace viper::codegen::x64
{

/// \brief Run conservative Machine IR peepholes for Phase A bring-up.
void runPeepholes(MFunction &fn);

} // namespace viper::codegen::x64
