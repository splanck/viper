//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/DCE.hpp
// Purpose: Declaration for x86-64 dead code elimination peephole sub-pass.
//          Delegates to the shared template in PeepholeDCE.hpp with x86-64
//          specific traits.
//
// Key invariants:
//   - RSP modifications are never eliminated.
//   - Iterates to a fixed point within each basic block.
//
// Ownership/Lifetime:
//   - Operates on mutable instructions owned by the caller.
//
// Links: src/codegen/common/PeepholeDCE.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "PeepholeCommon.hpp"

#include <cstddef>
#include <vector>

namespace viper::codegen::x64::peephole
{

/// @brief Run dead code elimination on a basic block.
/// @return Number of instructions eliminated.
std::size_t runBlockDCE(std::vector<MInstr> &instrs, PeepholeStats &stats);

} // namespace viper::codegen::x64::peephole
