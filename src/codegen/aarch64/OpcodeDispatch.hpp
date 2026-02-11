//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/OpcodeDispatch.hpp
// Purpose: Instruction lowering dispatch for IL->MIR conversion.
// Key invariants: Returns true when the opcode is handled, false otherwise;
//                 unhandled opcodes must be processed by the caller;
//                 block indices are used instead of references to survive
//                 emplace_back invalidation.
// Ownership/Lifetime: Stateless free function; mutable state is accessed
//                     through the LoweringContext reference.
// Links: codegen/aarch64/InstrLowering.hpp, codegen/aarch64/LoweringContext.hpp
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
/// @param bbOutIdx Index of the output block in ctx.mf.blocks. We use an index
///                 instead of a reference because instruction lowering can add
///                 trap blocks via emplace_back(), which invalidates references.
/// @returns true if the instruction was handled, false otherwise.
bool lowerInstruction(const il::core::Instr &ins,
                      const il::core::BasicBlock &bbIn,
                      LoweringContext &ctx,
                      std::size_t bbOutIdx);

} // namespace viper::codegen::aarch64
