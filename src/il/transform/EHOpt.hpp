//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/EHOpt.hpp
// Purpose: Exception handling optimization pass -- removes redundant eh.push/
//          eh.pop pairs when the protected region contains no instructions
//          that could throw, and eliminates dead handler blocks.
// Key invariants:
//   - Only removes EH pairs when provably no throwing instruction exists
//     between eh.push and eh.pop within the same block.
//   - Handler blocks are not removed directly; DCE cleans them up later.
// Ownership/Lifetime: Stateless module pass instantiated by the registry.
// Links: il/transform/PassRegistry.hpp, il/core/Opcode.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Module.hpp"

namespace il::transform {

/// @brief Remove redundant eh.push/eh.pop pairs from all functions.
/// @param module Module to optimize.
/// @return True if any EH pairs were removed.
bool ehOpt(il::core::Module &module);

} // namespace il::transform
