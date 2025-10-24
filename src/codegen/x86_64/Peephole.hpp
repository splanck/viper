// src/codegen/x86_64/Peephole.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare local Machine IR peephole optimisations for the x86-64
//          backend focused on eliminating redundant zero materialisations and
//          comparisons.
// Invariants: Transformations only rewrite well-formed register/immediate
//             pairs that match exact patterns, preserving operand order and
//             register classes for all other instructions.
// Ownership: Operates on Machine IR instructions in-place without owning the
//            surrounding function or blocks.
// Notes: Header depends solely on MachineIR.hpp and the standard library.

#pragma once

#include "MachineIR.hpp"

namespace viper::codegen::x64
{
/// \brief Apply a set of conservative peephole optimisations to a machine function.
void runPeepholes(MFunction &fn) noexcept;
} // namespace viper::codegen::x64
