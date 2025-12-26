//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/TerminatorLowering.hpp
// Purpose: Terminator instruction lowering for IL->MIR conversion.
//
// This header declares functions for lowering control-flow terminators
// (br, cbr, trap, switch) after all other instructions have been lowered.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "LoweringContext.hpp"
#include "MachineIR.hpp"
#include "il/core/Function.hpp"

#include <unordered_map>
#include <vector>

namespace viper::codegen::aarch64
{

/// @brief Lower control-flow terminators for all blocks in a function.
/// @details This must be called AFTER all other instructions have been lowered,
///          to ensure branches appear after the values they depend on are computed.
/// @param fn The IL function being lowered
/// @param mf The output MIR function
/// @param ti Target info for ABI register mappings
/// @param fb Frame builder for stack allocation
/// @param phiVregId Block label -> vreg IDs for phi parameters
/// @param phiRegClass Block label -> register classes for phi parameters
/// @param phiSpillOffset Block label -> spill offsets for phi parameters
/// @param blockTempVRegSnapshot Per-block tempVReg snapshots for correct vreg mappings
/// @param tempRegClass Temp ID -> register class mapping
/// @param nextVRegId Counter for vreg ID allocation
void lowerTerminators(const il::core::Function &fn,
                      MFunction &mf,
                      const TargetInfo &ti,
                      FrameBuilder &fb,
                      const std::unordered_map<std::string, std::vector<uint16_t>> &phiVregId,
                      const std::unordered_map<std::string, std::vector<RegClass>> &phiRegClass,
                      const std::unordered_map<std::string, std::vector<int>> &phiSpillOffset,
                      std::vector<std::unordered_map<unsigned, uint16_t>> &blockTempVRegSnapshot,
                      std::unordered_map<unsigned, RegClass> &tempRegClass,
                      uint16_t &nextVRegId);

} // namespace viper::codegen::aarch64
