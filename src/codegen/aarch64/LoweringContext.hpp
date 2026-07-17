//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

namespace zanna::codegen::aarch64 {

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

/// @brief Allocate or retrieve the frame spill slot for a cross-block IL temp.
/// @details All entry saves, liveness allocation, and cross-block reloads share
///          this helper so every IL temp maps to one stable spill slot.
inline int ensureCrossBlockSpill(FrameBuilder &fb, unsigned tempId) {
    return fb.ensureSpill(spillKeyForCrossBlockTemp(tempId));
}

/// @brief Ensure a materialized scalar operand is available in an FPR.
/// @details Integer operands feeding FP operations are converted with SCvtF;
///          existing FPR operands pass through unchanged.
inline uint16_t coerceScalarOperandToFpr(uint16_t vreg,
                                         RegClass &cls,
                                         uint16_t &nextVRegId,
                                         MBasicBlock &out) {
    if (cls != RegClass::GPR)
        return vreg;

    const uint16_t converted = allocateNextVReg(nextVRegId);
    out.instrs.push_back(MInstr{
        MOpcode::SCvtF,
        {MOperand::vregOp(RegClass::FPR, converted), MOperand::vregOp(RegClass::GPR, vreg)}});
    cls = RegClass::FPR;
    return converted;
}

/// @brief Deferred request for a per-function shared trap block.
/// @details Trap-guard lowering registers the trap kinds it branches to;
///          the blocks themselves are materialised once per function after
///          all instruction and terminator lowering has finished. Deferring
///          creation means opcode handlers never append to MFunction::blocks
///          while holding MBasicBlock references (vector growth would
///          invalidate them), and each trap kind costs one block per function
///          instead of one per guarded instruction.
struct TrapBlockRequest {
    std::string label{};  ///< Block label the guard branches to.
    std::string callee{}; ///< Non-empty: body is `bl <callee>` (no-return).
    int raiseCode{0};     ///< Used when @ref callee is empty: body raises
                          ///< rt_trap_raise_error with this error code.
};

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

    /// @brief Per-function shared trap-block requests, keyed by trap kind.
    /// @details Materialised at the end of lowerFunction; see TrapBlockRequest.
    std::unordered_map<std::string, TrapBlockRequest> &sharedTrapBlocks;

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
    /// @param trapBlockRequests Per-function shared trap-block request registry.
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
                    std::unordered_map<std::string, TrapBlockRequest> &trapBlockRequests)
        : fn(function), ti(targetInfo), fb(frameBuilder), mf(machineFunction),
          nextVRegId(nextVirtualRegId), tempVReg(tempVirtualRegs), tempRegClass(tempClasses),
          phiVregId(phiVirtualRegs), phiRegClass(phiClasses), phiSpillOffset(phiSpillOffsets),
          crossBlockSpillOffset(crossBlockSpillOffsets), tempDefBlock(tempDefinitionBlocks),
          crossBlockTemps(crossBlockLiveTemps), stringLiteralByteLengths(stringLiteralLengths),
          knownVarArgNamedArgCounts(varArgNamedArgCounts), sharedTrapBlocks(trapBlockRequests) {}

    /// @brief Retrieve the MIR basic block at the given index.
    /// @param idx Zero-based index into the function's block list.
    /// @return Reference to the corresponding MBasicBlock.
    MBasicBlock &bbOut(std::size_t idx) {
        return mf.blocks[idx];
    }
};

/// @brief Register (or look up) the per-function shared trap block for @p kind.
/// @details Returns the label a trap guard should branch to. The block itself
///          is materialised once after lowering finishes, so calling this never
///          touches MFunction::blocks and is safe while MBasicBlock references
///          are live. Kinds with identical bodies share one block per function.
/// @param ctx       Active lowering context.
/// @param kind      Stable trap kind key (e.g. "div0", "ovf", "bounds").
/// @param callee    No-return runtime entry point for `bl`-style bodies, or
///                  nullptr to emit a rt_trap_raise_error body instead.
/// @param raiseCode Error code for rt_trap_raise_error bodies (ignored when
///                  @p callee is non-null).
/// @return Label of the shared trap block for @p kind.
inline const std::string &requestSharedTrapBlock(LoweringContext &ctx,
                                                 const char *kind,
                                                 const char *callee,
                                                 int raiseCode = 0) {
    auto it = ctx.sharedTrapBlocks.find(kind);
    if (it == ctx.sharedTrapBlocks.end()) {
        TrapBlockRequest request{};
        request.label = std::string(".Ltrap_") + kind;
        request.callee = callee != nullptr ? callee : "";
        request.raiseCode = raiseCode;
        it = ctx.sharedTrapBlocks.emplace(kind, std::move(request)).first;
    }
    return it->second.label;
}

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

} // namespace zanna::codegen::aarch64
