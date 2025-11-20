//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Dead Store Elimination (DSE) — function-level pass
// Removes stores that are provably overwritten before being read, using a
// simple backward scan within each basic block and conservative clobbering
// on calls. Aliasing queries rely on BasicAA when available.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Function.hpp"
#include "il/transform/AnalysisManager.hpp"

namespace il::transform
{

/// \brief Eliminate trivially dead stores inside a function.
/// \details Performs a backward walk per basic block. A store to address P is
/// considered dead if no intervening load of an alias of P occurs before a
/// subsequent store to an alias of P. Calls conservatively clobber memory when
/// they may Mod/Ref according to BasicAA's ModRef classification.
/// The transformation is intentionally conservative and intra-block to keep the
/// build fast and safe.
/// \param F Function to transform in place.
/// \param AM Analysis manager for alias/modref queries.
/// \return True when any store was removed.
bool runDSE(il::core::Function &F, AnalysisManager &AM);

} // namespace il::transform
