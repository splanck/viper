//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/PreRegAllocOpt.hpp
// Purpose: Conservative AArch64 MIR cleanup before register allocation:
//          identity-move removal, dead phi-store elimination, and copy coalescing.
//
// Key invariants:
//   - Must run after LegalizePass and before RegAllocPass.
//   - Operates on virtual registers only; physical register allocation has not
//     yet occurred.
//
// Ownership/Lifetime:
//   - Borrows MFunction for the duration of the call; no persistent state.
//
// Links: codegen/aarch64/PreRegAllocOpt.cpp,
//        codegen/aarch64/passes/PreRegAllocOptPass.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

#include <cstddef>

namespace zanna::codegen::aarch64 {

/// @brief Run conservative pre-register-allocation copy cleanup.
/// @param fn Machine function to rewrite in place.
/// @return Number of MIR instructions removed.
std::size_t runPreRegAllocOpt(MFunction &fn);

} // namespace zanna::codegen::aarch64
