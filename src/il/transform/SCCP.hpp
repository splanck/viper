// File: src/il/transform/SCCP.hpp
// Purpose: Declare the sparse conditional constant propagation pass for IL.
// Key invariants: Pass conservatively assumes unknown values are overdefined until proven constant; 
//                 operates on block parameters as SSA phi nodes.
// Ownership/Lifetime: Pass mutates caller-owned modules in place.
// Links: docs/codemap.md
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
