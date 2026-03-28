//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/OperandRoles.hpp
// Purpose: Determines whether each operand of an MIR instruction is a use,
//          a def, or both, for register allocation purposes.
// Key invariants:
//   - Must cover every MOpcode that has register operands.
//   - Returns {isUse, isDef} for the operand at position idx.
// Ownership/Lifetime:
//   - Stateless free function; no ownership.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <utility>

#include "codegen/aarch64/MachineIR.hpp"

namespace viper::codegen::aarch64::ra {

/// @brief Determine the use/def roles of operand @p idx in instruction @p ins.
/// @return {isUse, isDef} pair.
std::pair<bool, bool> operandRoles(const MInstr &ins, std::size_t idx);

} // namespace viper::codegen::aarch64::ra
