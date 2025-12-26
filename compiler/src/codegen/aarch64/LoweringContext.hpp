//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LoweringContext.hpp
// Purpose: Shared state and helpers for IL->MIR lowering on AArch64.
//
// This header defines the LoweringContext struct which encapsulates all the
// mutable state needed during instruction lowering, avoiding long parameter
// lists and enabling cleaner extraction of opcode handlers.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "FrameBuilder.hpp"
#include "MachineIR.hpp"
#include "TargetAArch64.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64
{

/// @brief Encapsulates all mutable state needed during IL->MIR lowering.
///
/// This context is passed to opcode handlers to avoid long parameter lists.
/// It contains references to the target info, frame builder, and various
/// maps tracking temp-to-vreg mappings, phi spill slots, and cross-block temps.
struct LoweringContext
{
    // Target information
    const TargetInfo &ti;

    // Frame builder for stack allocation
    FrameBuilder &fb;

    // Output MIR function
    MFunction &mf;

    // Current vreg ID counter
    uint16_t &nextVRegId;

    // Temp ID -> vreg ID mapping (function-wide)
    std::unordered_map<unsigned, uint16_t> &tempVReg;

    // Temp ID -> register class (GPR or FPR)
    std::unordered_map<unsigned, RegClass> &tempRegClass;

    // Block label -> vreg IDs for phi parameters
    std::unordered_map<std::string, std::vector<uint16_t>> &phiVregId;

    // Block label -> register classes for phi parameters
    std::unordered_map<std::string, std::vector<RegClass>> &phiRegClass;

    // Block label -> spill offsets for phi parameters
    std::unordered_map<std::string, std::vector<int>> &phiSpillOffset;

    // Cross-block temps: temp ID -> spill offset
    std::unordered_map<unsigned, int> &crossBlockSpillOffset;

    // Temp ID -> defining block index
    std::unordered_map<unsigned, std::size_t> &tempDefBlock;

    // Set of temps that are used across block boundaries
    std::unordered_set<unsigned> &crossBlockTemps;

    // Trap label counter for unique labels
    unsigned &trapLabelCounter;

    /// @brief Get MIR block by index
    MBasicBlock &bbOut(std::size_t idx)
    {
        return mf.blocks[idx];
    }
};

/// @brief Find the index of a parameter in a basic block by temp ID.
/// @returns Parameter index (0-based) or -1 if not found.
inline int indexOfParam(const il::core::BasicBlock &bb, unsigned tempId)
{
    for (size_t i = 0; i < bb.params.size(); ++i)
        if (bb.params[i].id == tempId)
            return static_cast<int>(i);
    return -1;
}

/// @brief Find the producing instruction for a temp ID in a function.
/// @returns Pointer to the instruction, or nullptr if not found.
inline const il::core::Instr *findProducerInFunction(const il::core::Function &fn, unsigned tempId)
{
    for (const auto &bb : fn.blocks)
    {
        for (const auto &ins : bb.instructions)
        {
            if (ins.result && *ins.result == tempId)
                return &ins;
        }
    }
    return nullptr;
}

/// @brief Check if a basic block contains side-effecting instructions.
inline bool hasSideEffects(const il::core::BasicBlock &bb)
{
    for (const auto &ins : bb.instructions)
    {
        switch (ins.op)
        {
            case il::core::Opcode::Store:
            case il::core::Opcode::Call:
            case il::core::Opcode::Trap:
            case il::core::Opcode::TrapFromErr:
                return true;
            default:
                break;
        }
    }
    return false;
}

/// @brief Helper describing a lowered call sequence.
struct LoweredCall
{
    std::vector<MInstr> prefix;  // arg materialization and marshalling
    MInstr call;                 // Bl callee
    std::vector<MInstr> postfix; // any clean-up (e.g., stack restore)
};

} // namespace viper::codegen::aarch64
