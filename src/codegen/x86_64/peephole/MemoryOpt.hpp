//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/MemoryOpt.hpp
// Purpose: x86-64 memory peephole optimizations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "PeepholeCommon.hpp"

#include <cstddef>
#include <vector>

namespace viper::codegen::x64::peephole {

std::size_t eliminateDeadFrameStores(std::vector<MInstr> &instrs, PeepholeStats &stats);
std::size_t forwardFrameStoreLoads(std::vector<MInstr> &instrs, PeepholeStats &stats);

} // namespace viper::codegen::x64::peephole
