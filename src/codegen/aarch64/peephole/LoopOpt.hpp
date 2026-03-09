//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/LoopOpt.hpp
// Purpose: Declarations for loop-invariant constant hoisting peephole sub-pass.
//
// Key invariants:
//   - Only hoists MovRI to callee-saved registers (x19-x28).
//   - The register must be defined only by MovRI with the same immediate value
//     throughout the loop body.
//
// Ownership/Lifetime:
//   - Operates on mutable MFunction owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"

#include <cstddef>

namespace viper::codegen::aarch64::peephole
{

/// @brief Hoist loop-invariant MovRI instructions from loop bodies to preheaders.
std::size_t hoistLoopConstants(MFunction &fn);

} // namespace viper::codegen::aarch64::peephole
