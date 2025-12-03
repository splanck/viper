//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/OpcodeDispatch.hpp
// Purpose: Instruction lowering dispatch for IL->MIR conversion.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "LoweringContext.hpp"
#include "MachineIR.hpp"
#include "il/core/Instr.hpp"

namespace viper::codegen::aarch64
{

/// @brief Lower a single IL instruction to MIR.
/// @details This function handles a subset of opcodes that have been extracted
///          to reduce the size of lowerFunction(). Opcodes not handled here
///          will return false to indicate the caller should handle them.
/// @returns true if the instruction was handled, false otherwise.
bool lowerInstruction(const il::core::Instr &ins,
                      const il::core::BasicBlock &bbIn,
                      LoweringContext &ctx,
                      MBasicBlock &bbOutRef);

} // namespace viper::codegen::aarch64
