//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LoweringContext.hpp
// Purpose: Shared state and helpers for IL->MIR lowering on AArch64.
// Key invariants:
//   - Context references are valid for the duration of a single lowerFunction().
//   - Maps are populated incrementally as instructions are lowered.
//   - Cross-block temps are spilled to frame slots before successor blocks.
// Ownership/Lifetime:
//   - LoweringContext holds non-owning references; caller owns all state.
// Links: codegen/aarch64/LowerILToMIR.hpp,
//        codegen/aarch64/InstrLowering.hpp,
//        codegen/aarch64/FrameBuilder.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "FrameBuilder.hpp"
#include "MachineIR.hpp"
#include "TargetAArch64.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64 {

/// ID range: [1, kPhiVRegStart) for normal vregs.
inline constexpr uint16_t kFirstVirtualRegId = 1;
/// ID range: [kPhiVRegStart, kCrossBlockSpillKeyStart) for phi-parameter vregs.
inline constexpr uint16_t kPhiVRegStart = 40000;
/// Spill key base: [kCrossBlockSpillKeyStart, ...) encodes cross-block temp IDs.
inline constexpr uint32_t kCrossBlockSpillKeyStart = 50000;

/// @brief Allocate the next normal virtual register ID; throws if the phi range is reached.
inline uint16_t allocateNextVReg(uint16_t &nextVRegId) {
    if (nextVRegId >= kPhiVRegStart)
        throw std::runtime_error(
            "AArch64 lowering: virtual register space exhausted before phi spill range");
    return nextVRegId++;
}

/// @brief Allocate the next phi-parameter virtual register ID; throws on overflow.
inline uint16_t allocatePhiVReg(uint16_t &phiNextId) {
    if (phiNextId >= kCrossBlockSpillKeyStart)
        throw std::runtime_error(
            "AArch64 lowering: phi virtual register space exhausted before spill-key range");
    return phiNextId++;
}

/// @brief Map a cross-block temp ID to a unique FrameBuilder spill key.
/// @details The spill key range [kCrossBlockSpillKeyStart, ...) is reserved for
///          cross-block temps; it does not overlap with normal vreg IDs.
inline uint32_t spillKeyForCrossBlockTemp(unsigned tempId) {
    if (tempId > (std::numeric_limits<uint32_t>::max)() - kCrossBlockSpillKeyStart)
        throw std::runtime_error("AArch64 lowering: cross-block spill key overflow");
    return kCrossBlockSpillKeyStart + tempId;
}

/// @brief Encapsulates all mutable state needed during IL->MIR lowering.
///
/// This context is passed to opcode handlers to avoid long parameter lists.
/// It contains references to the target info, frame builder, and various
/// maps tracking temp-to-vreg mappings, phi spill slots, and cross-block temps.
struct LoweringContext {
    /// @brief IL function currently being lowered.
    const il::core::Function &fn;

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

    /// @brief Optional map from IL global string names to their byte lengths.
    const std::unordered_map<std::string, std::size_t> *stringLiteralByteLengths = nullptr;

    /// @brief Optional map from direct callee names to their named-argument counts.
    const std::unordered_map<std::string, std::size_t> *knownVarArgNamedArgCounts = nullptr;

    /// @brief Counter used to generate unique trap label names.
    unsigned &trapLabelCounter;

    /// @brief Construct a lowering context with every borrowed state object bound explicitly.
    /// @details The context stores references into the surrounding lowering pass and must never
    ///          outlive that pass invocation. Using an explicit constructor keeps the long member
    ///          list checked in one place and prevents accidental default construction of required
    ///          reference state.
    /// @param function IL function currently being lowered.
    /// @param targetInfo ABI and register information for the AArch64 target.
    /// @param frameBuilder Frame-layout allocator used while lowering.
    /// @param machineFunction Output MIR function being built.
    /// @param nextVirtualRegId Counter used to allocate new virtual registers.
    /// @param tempVirtualRegs Function-wide mapping from IL temp IDs to vreg IDs.
    /// @param tempClasses Function-wide mapping from IL temp IDs to register classes.
    /// @param phiVirtualRegs Phi-parameter vreg IDs by block label.
    /// @param phiClasses Phi-parameter register classes by block label.
    /// @param phiSpillOffsets Phi spill-slot offsets by block label.
    /// @param crossBlockSpillOffsets Spill slots for temps live across blocks.
    /// @param tempDefinitionBlocks Basic-block index that defines each temp.
    /// @param crossBlockLiveTemps Temps proven live across basic blocks.
    /// @param stringLiteralLengths Optional global string literal byte-length table.
    /// @param varArgNamedArgCounts Optional direct-callee named-argument count table.
    /// @param trapCounter Counter for unique trap label generation.
    LoweringContext(const il::core::Function &function,
                    const TargetInfo &targetInfo,
                    FrameBuilder &frameBuilder,
                    MFunction &machineFunction,
                    uint16_t &nextVirtualRegId,
                    std::unordered_map<unsigned, uint16_t> &tempVirtualRegs,
                    std::unordered_map<unsigned, RegClass> &tempClasses,
                    std::unordered_map<std::string, std::vector<uint16_t>> &phiVirtualRegs,
                    std::unordered_map<std::string, std::vector<RegClass>> &phiClasses,
                    std::unordered_map<std::string, std::vector<int>> &phiSpillOffsets,
                    std::unordered_map<unsigned, int> &crossBlockSpillOffsets,
                    std::unordered_map<unsigned, std::size_t> &tempDefinitionBlocks,
                    std::unordered_set<unsigned> &crossBlockLiveTemps,
                    const std::unordered_map<std::string, std::size_t> *stringLiteralLengths,
                    const std::unordered_map<std::string, std::size_t> *varArgNamedArgCounts,
                    unsigned &trapCounter)
        : fn(function), ti(targetInfo), fb(frameBuilder), mf(machineFunction),
          nextVRegId(nextVirtualRegId), tempVReg(tempVirtualRegs), tempRegClass(tempClasses),
          phiVregId(phiVirtualRegs), phiRegClass(phiClasses), phiSpillOffset(phiSpillOffsets),
          crossBlockSpillOffset(crossBlockSpillOffsets), tempDefBlock(tempDefinitionBlocks),
          crossBlockTemps(crossBlockLiveTemps), stringLiteralByteLengths(stringLiteralLengths),
          knownVarArgNamedArgCounts(varArgNamedArgCounts), trapLabelCounter(trapCounter) {}

    /// @brief Retrieve the MIR basic block at the given index.
    /// @param idx Zero-based index into the function's block list.
    /// @return Reference to the corresponding MBasicBlock.
    MBasicBlock &bbOut(std::size_t idx) {
        return mf.blocks[idx];
    }
};

/// @brief Find the index of a parameter in a basic block by temp ID.
/// @param bb     The basic block whose parameter list is searched.
/// @param tempId The IL temp ID to locate.
/// @return Parameter index (0-based) or -1 if not found.
inline int indexOfParam(const il::core::BasicBlock &bb, unsigned tempId) {
    for (size_t i = 0; i < bb.params.size(); ++i)
        if (bb.params[i].id == tempId)
            return static_cast<int>(i);
    return -1;
}

/// @brief Find the producing instruction for a temp ID in a function.
/// @param fn     The IL function to search across all basic blocks.
/// @param tempId The IL temp ID whose defining instruction is sought.
/// @return Pointer to the instruction, or nullptr if not found.
inline const il::core::Instr *findProducerInFunction(const il::core::Function &fn,
                                                     unsigned tempId) {
    for (const auto &bb : fn.blocks) {
        for (const auto &ins : bb.instructions) {
            if (ins.result && *ins.result == tempId)
                return &ins;
        }
    }
    return nullptr;
}

/// @brief Check if a basic block contains side-effecting instructions.
/// @param bb The basic block to inspect for stores, calls, or traps.
/// @return True if any instruction in the block has observable side effects.
inline bool hasSideEffects(const il::core::BasicBlock &bb) {
    for (const auto &ins : bb.instructions) {
        switch (ins.op) {
            case il::core::Opcode::Ret:
            case il::core::Opcode::Br:
            case il::core::Opcode::CBr:
                continue;
            default:
                break;
        }
        if (il::core::getOpcodeInfo(ins.op).hasSideEffects)
            return true;
        if (il::core::memoryEffects(ins.op) != il::core::MemoryEffects::None)
            return true;
    }
    return false;
}

/// @brief Helper describing a lowered call sequence.
/// @details Splits the MIR for a call into three phases: prefix instructions
///          that materialise and marshal arguments into ABI registers/stack
///          slots, the actual BL instruction, and postfix instructions that
///          perform any required clean-up (e.g. restoring the stack pointer).
struct LoweredCall {
    std::vector<MInstr> prefix;  ///< Argument materialisation and marshalling instructions.
    MInstr call;                 ///< The BL (branch-with-link) callee instruction.
    std::vector<MInstr> postfix; ///< Post-call clean-up (e.g. stack restore).
};

} // namespace viper::codegen::aarch64
