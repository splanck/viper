//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/Reassociate.hpp
// Purpose: Algebraic reassociation pass for commutative+associative integer
//          operations. Flattens expression trees and sorts operands into a
//          canonical rank order so that equivalent expressions share the same
//          structure, enabling better CSE and constant folding.
// Key invariants:
//   - Only reassociates provably associative opcodes (Add, Mul, And, Or, Xor).
//   - Does NOT reassociate floating-point operations (not associative under
//     IEEE 754) or checked-overflow operations (reassociation could mask traps).
//   - Preserves SSA form by replacing uses via UseDefInfo.
// Ownership/Lifetime: Free function operating on a caller-owned Module.
// Links: il/core/fwd.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"

namespace il::transform {

/// @brief Reassociate commutative+associative integer operations.
/// @details Flattens chains like `(a + b) + c` and sorts operands by a
///          canonical rank (constants last, higher temp IDs first) so that
///          equivalent expressions share the same operand order for CSE.
/// @param M Module to transform in place.
void reassociate(il::core::Module &M);

} // namespace il::transform
