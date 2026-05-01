//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/PreRegAllocOpt.hpp
// Purpose: Conservative AArch64 MIR cleanup before register allocation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

#include <cstddef>

namespace viper::codegen::aarch64 {

/// @brief Run conservative pre-register-allocation copy cleanup.
/// @param fn Machine function to rewrite in place.
/// @return Number of MIR instructions removed.
std::size_t runPreRegAllocOpt(MFunction &fn);

} // namespace viper::codegen::aarch64
