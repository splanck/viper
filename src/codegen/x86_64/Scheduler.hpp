//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Scheduler.hpp
// Purpose: Declare the post-RA x86-64 instruction scheduler.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"

#include <cstddef>
#include <vector>

namespace viper::codegen::x64 {

/// \brief Run conservative post-register-allocation scheduling for one function.
/// \return Number of basic-block segments whose instruction order changed.
std::size_t scheduleFunction(MFunction &fn);

/// \brief Run post-register-allocation scheduling for a MIR module.
std::size_t scheduleModule(std::vector<MFunction> &mir);

} // namespace viper::codegen::x64

