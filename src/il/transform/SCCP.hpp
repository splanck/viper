//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Sparse Conditional Constant Propagation (SCCP) optimization
// pass for IL modules. SCCP is a powerful dataflow analysis that simultaneously
// performs constant propagation and dead code elimination using sparse analysis
// techniques.
//
// SCCP improves on simple constant folding by tracking constants through the
// control flow graph, evaluating branches with constant conditions, and discovering
// constants at control flow merge points (block parameters). The "sparse" approach
// only analyzes executable paths, avoiding work on provably dead code. By combining
// constant propagation with executable region analysis, SCCP finds optimization
// opportunities that simpler passes miss.
//
// Key Responsibilities:
// - Track constant values through SSA temporaries and block parameters
// - Identify executable and non-executable control flow edges
// - Fold instructions when all operands become constant
// - Evaluate conditional branches with constant conditions
// - Rewrite block parameters when all incoming values are the same constant
// - Replace uses of discovered constants throughout the function
//
// Design Notes:
// The implementation uses a worklist algorithm with three lattice states per value:
// Bottom (unknown/undefined), Constant (known constant), Top (overdefined/varying).
// Block parameters act as SSA phi nodes, merging values from executable predecessors
// only. The pass is conservative, assuming values are overdefined unless proven
// constant. After analysis converges, the module is rewritten to reflect discovered
// constants and eliminated dead branches.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"

namespace il::transform
{

/// \brief Propagate constants through the IL using sparse conditional evaluation.
///
/// \details Identifies executable regions of the CFG, evaluates instructions whose
/// operands become constant, folds conditional branches, and rewrites uses of
/// discovered constants.  Block parameters are treated as SSA phi nodes whose
/// meet only considers executable predecessors.
///
/// \param module Module optimised in place.
void sccp(core::Module &module);

} // namespace il::transform
