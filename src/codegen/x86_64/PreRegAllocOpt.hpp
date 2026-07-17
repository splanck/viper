//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/PreRegAllocOpt.hpp
// Purpose: Conservative x86-64 MIR cleanup before register allocation.
// Key invariants:
//   - Runs only on virtual-register MIR, before physical registers are assigned.
//   - Only removes provably dead copy-chains; semantics are never altered.
// Ownership/Lifetime:
//   - Stateless; mutates caller-owned MFunction in place.
// Links: codegen/x86_64/PreRegAllocOpt.cpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/MachineIR.hpp"

#include <cstddef>

namespace zanna::codegen::x64 {

/// @brief Run conservative pre-register-allocation copy cleanup.
/// @param fn Machine function to rewrite in place.
/// @return Number of MIR instructions removed.
std::size_t runPreRegAllocOpt(MFunction &fn);

} // namespace zanna::codegen::x64
