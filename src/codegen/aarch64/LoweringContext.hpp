//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LoweringContext.hpp
// Purpose: Shared state and helpers for IL->MIR lowering on AArch64.
// Key invariants: Context references are valid for the duration of a single
//                 function lowering invocation; maps are populated
//                 incrementally as instructions are lowered; cross-block temps
//                 are spilled to frame slots before successor blocks.
// Ownership/Lifetime: LoweringContext holds references to externally-owned
//                     state; it does not manage lifetimes of maps or builders.
// Links: docs/architecture.md
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
    /// @brief ABI and register information for the AArch64 target.
    const TargetInfo &ti;

    /// @brief Frame builder for stack slot allocation and layout.
    FrameBuilder &fb;

    /// @brief Output MIR function being constructed during lowering.
    MFunction &mf;

    /// @brief Monotonically increasing counter for minting virtual register IDs.
    uint16_t &nextVRegId;

    /// @brief Maps IL temp IDs to allocated virtual register IDs (function-wide).
    std::unordered_map<unsigned, uint16_t> &tempVReg;

    /// @brief Maps IL temp IDs to their register class (GPR or FPR).
    std::unordered_map<unsigned, RegClass> &tempRegClass;

    /// @brief Maps block labels to the vreg IDs assigned to their phi parameters.
    std::unordered_map<std::string, std::vector<uint16_t>> &phiVregId;

    /// @brief Maps block labels to the register classes of their phi parameters.
    std::unordered_map<std::string, std::vector<RegClass>> &phiRegClass;

    /// @brief Maps block labels to spill slot offsets for their phi parameters.
    std::unordered_map<std::string, std::vector<int>> &phiSpillOffset;

    /// @brief Maps cross-block temp IDs to their allocated spill slot offsets.
    std::unordered_map<unsigned, int> &crossBlockSpillOffset;

    /// @brief Maps temp IDs to the index of the basic block that defines them.
    std::unordered_map<unsigned, std::size_t> &tempDefBlock;

    /// @brief Set of temp IDs whose values are live across block boundaries.
    std::unordered_set<unsigned> &crossBlockTemps;

    /// @brief Counter used to generate unique trap label names.
    unsigned &trapLabelCounter;

    /// @brief Retrieve the MIR basic block at the given index.
    /// @param idx Zero-based index into the function's block list.
    /// @return Reference to the corresponding MBasicBlock.
    MBasicBlock &bbOut(std::size_t idx)
    {
        return mf.blocks[idx];
    }
};

/// @brief Find the index of a parameter in a basic block by temp ID.
/// @param bb     The basic block whose parameter list is searched.
/// @param tempId The IL temp ID to locate.
/// @return Parameter index (0-based) or -1 if not found.
inline int indexOfParam(const il::core::BasicBlock &bb, unsigned tempId)
{
    for (size_t i = 0; i < bb.params.size(); ++i)
        if (bb.params[i].id == tempId)
            return static_cast<int>(i);
    return -1;
}

/// @brief Find the producing instruction for a temp ID in a function.
/// @param fn     The IL function to search across all basic blocks.
/// @param tempId The IL temp ID whose defining instruction is sought.
/// @return Pointer to the instruction, or nullptr if not found.
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
/// @param bb The basic block to inspect for stores, calls, or traps.
/// @return True if any instruction in the block has observable side effects.
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
/// @details Splits the MIR for a call into three phases: prefix instructions
///          that materialise and marshal arguments into ABI registers/stack
///          slots, the actual BL instruction, and postfix instructions that
///          perform any required clean-up (e.g. restoring the stack pointer).
struct LoweredCall
{
    std::vector<MInstr> prefix;  ///< Argument materialisation and marshalling instructions.
    MInstr call;                 ///< The BL (branch-with-link) callee instruction.
    std::vector<MInstr> postfix; ///< Post-call clean-up (e.g. stack restore).
};

} // namespace viper::codegen::aarch64
