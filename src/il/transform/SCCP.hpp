//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/SCCP.hpp
// Purpose: Sparse Conditional Constant Propagation -- worklist-based dataflow
//          analysis that simultaneously performs constant propagation and dead
//          branch elimination using three-state lattice (Bottom/Constant/Top).
//          Block parameters are treated as SSA phi nodes merging values from
//          executable predecessors only.
// Key invariants:
//   - Conservative: values are assumed overdefined unless proven constant.
//   - Only executable CFG edges are analysed; dead code is skipped.
// Ownership/Lifetime: Free function operating on a caller-owned Module.
// Links: il/core/fwd.hpp
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
