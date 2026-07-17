//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/ra/OperandRoles.hpp
// Purpose: Determines whether each operand of an MIR instruction is a use,
//          a def, or both, for register allocation purposes.
//
// Key invariants:
//   - Must cover every MOpcode that has register operands.
//   - Returns {isUse, isDef} for the operand at position idx.
//
// Ownership/Lifetime:
//   - Stateless free function; no ownership.
//
// Links: codegen/aarch64/ra/OperandRoles.cpp,
//        codegen/aarch64/ra/Allocator.cpp,
//        codegen/aarch64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <utility>

#include "codegen/aarch64/MachineIR.hpp"

namespace zanna::codegen::aarch64::ra {

/// @brief Determine the use/def roles of operand @p idx in instruction @p ins.
/// @details Routes through the opcode-class predicates in `OpcodeClassify.hpp`
///          to classify each operand of every supported `MOpcode`. Used by the
///          register allocator's liveness pass to build gen/kill sets per block.
/// @param ins Machine instruction whose operand is being classified.
/// @param idx Zero-based index into `ins.ops`.
/// @return `{isUse, isDef}` pair indicating whether the operand is read, written, or both.
std::pair<bool, bool> operandRoles(const MInstr &ins, std::size_t idx);

} // namespace zanna::codegen::aarch64::ra
