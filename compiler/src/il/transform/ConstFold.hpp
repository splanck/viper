//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the constant folding optimization pass for IL modules.
// Constant folding replaces instructions with known constant operands by their
// computed constant results, eliminating runtime computation overhead.
//
// The IL uses SSA form where values flow through temporary assignments. When
// an instruction's operands are all literal constants, the compiler can evaluate
// the instruction at compile time and replace it with its result. This pass
// identifies and folds such trivially computable instructions across integer
// arithmetic, bitwise operations, and selected math intrinsics.
//
// Key Responsibilities:
// - Fold integer arithmetic (add, sub, mul, div, rem) with constant operands
// - Fold bitwise operations (and, or, xor, shifts) with constant operands
// - Fold comparison operations with constant operands to boolean results
// - Fold selected math intrinsics (abs, min, max) with constant arguments
// - Replace uses of folded instructions with computed literal values
//
// Design Notes:
// The pass operates conservatively, only folding operations where both operands
// are literals and the operation is guaranteed not to trap (or trapping behavior
// is acceptable at compile time). The implementation mutates the module in place,
// simplifying instructions directly within their basic blocks. No dataflow
// analysis is performed - only single-instruction constant propagation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"

namespace il::transform
{

/// \brief Fold trivial constant computations in @p m.
void constFold(core::Module &m);

} // namespace il::transform
