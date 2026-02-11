//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/DSE.hpp
// Purpose: Dead Store Elimination -- function-level pass that removes stores
//          provably overwritten before being read. Intra-block DSE uses a
//          backward scan; cross-block DSE uses forward dataflow on
//          non-escaping allocas. Aliasing queries rely on BasicAA.
// Key invariants:
//   - Only removes stores that are provably dead (overwritten on all paths).
//   - Calls conservatively clobber memory per BasicAA ModRef classification.
// Ownership/Lifetime: Free functions operating on caller-owned Function and
//          AnalysisManager references.
// Links: il/core/Function.hpp, il/transform/AnalysisManager.hpp,
//        il/analysis/BasicAA.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Function.hpp"
#include "il/transform/AnalysisManager.hpp"

namespace il::transform
{

/// \brief Eliminate trivially dead stores inside a function (intra-block).
/// \details Performs a backward walk per basic block. A store to address P is
/// considered dead if no intervening load of an alias of P occurs before a
/// subsequent store to an alias of P. Calls conservatively clobber memory when
/// they may Mod/Ref according to BasicAA's ModRef classification.
/// \param F Function to transform in place.
/// \param AM Analysis manager for alias/modref queries.
/// \return True when any store was removed.
bool runDSE(il::core::Function &F, AnalysisManager &AM);

/// \brief Eliminate dead stores across basic block boundaries.
/// \details Uses forward dataflow to identify stores to non-escaping allocas
/// that are overwritten on all paths before being read. This extends intra-block
/// DSE to handle cases like:
/// - Stores followed by conditional branches where all paths overwrite
/// - Stores to variables that are reassigned before function exit
/// \param F Function to transform in place.
/// \param AM Analysis manager for alias/modref queries.
/// \return True when any store was removed.
bool runCrossBlockDSE(il::core::Function &F, AnalysisManager &AM);

} // namespace il::transform
