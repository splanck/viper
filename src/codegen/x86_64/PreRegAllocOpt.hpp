//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/PreRegAllocOpt.hpp
// Purpose: Conservative x86-64 MIR cleanup before register allocation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/MachineIR.hpp"

#include <cstddef>

namespace viper::codegen::x64 {

/// @brief Run conservative pre-register-allocation copy cleanup.
/// @param fn Machine function to rewrite in place.
/// @return Number of MIR instructions removed.
std::size_t runPreRegAllocOpt(MFunction &fn);

} // namespace viper::codegen::x64
