//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/ra/Allocator.hpp
// Purpose: Core linear-scan register allocator class for AArch64. Owns all
//          allocation state and drives the per-block, per-instruction
//          allocation loop.
//
// Key invariants:
//   - After run(), every MReg in the MFunction has isPhys=true.
//   - Spill slots are allocated via FrameBuilder; callee-saved usage is
//     recorded for prologue/epilogue generation.
//   - Cross-block register persistence uses single-predecessor exit states.
//
// Ownership/Lifetime:
//   - Constructed per-function; borrows MFunction and TargetInfo references.
//   - Must not outlive the MFunction it modifies.
//
// Design note (spill organization):
//   Spill decisions are intentionally interleaved with the linear-scan loop here rather than
//   factored into a standalone "Spiller" stage like the x86-64 backend (codegen/x86_64/ra/
//   Spiller). Victim selection reads live per-instruction allocator state (next-use distance,
//   live-out, dirty flags, register pools) and inserts reload/store code in place, so coupling
//   avoids threading that mutable state across a stage boundary. The two backends deliberately
//   differ; spill correctness here is covered by the AArch64 spill/pressure tests
//   (Arm64SpillFPR.*, Arm64CrossBlockPhi.*, test_aarch64_frame_spill_reuse) and the VM<->native
//   differential gate, so this is not a missing stage to be "completed".
//
// Links: codegen/aarch64/ra/Allocator.cpp,
//        codegen/aarch64/RegAllocLinear.hpp,
//        codegen/aarch64/ra/Liveness.hpp,
//        codegen/aarch64/ra/RegPools.hpp,
//        codegen/aarch64/ra/VState.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Liveness.hpp"
#include "RegPools.hpp"
#include "VState.hpp"
#include "codegen/aarch64/FrameBuilder.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/RegAllocLinear.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64::ra {

/// @brief Per-function linear-scan register allocator for AArch64 MIR.
///
/// Drives the allocation loop over all basic blocks and instructions.
/// Virtual registers are mapped to physical registers or spill slots; the
/// resulting AllocationResult carries callee-saved register usage and
/// FrameBuilder state for prologue/epilogue emission.
class LinearAllocator {
  public:
    /// @brief Construct an allocator for @p fn using target register constraints from @p ti.
    LinearAllocator(MFunction &fn, const TargetInfo &ti);

    /// @brief Run the allocator over all blocks and return results.
    AllocationResult run();

  private:
    MFunction &fn_;
    const TargetInfo &ti_;
    FrameBuilder fb_;
    RegPools pools_;
    std::unordered_map<uint16_t, VState> gprStates_;
    std::unordered_map<uint16_t, VState> fprStates_;
    unsigned currentInstrIdx_{0};        ///< Current instruction index for LRU tracking.
    std::size_t currentBlockIdx_{0};     ///< Current block index for liveness lookups.
    unsigned currentBlockInstrCount_{0}; ///< Instruction count of the current block.
    std::unordered_map<uint16_t, std::vector<unsigned>>
        usePositionsGPR_; ///< All use positions for GPR vregs.
    std::unordered_map<uint16_t, std::vector<unsigned>>
        usePositionsFPR_;                 ///< All use positions for FPR vregs.
    std::vector<unsigned> callPositions_; ///< Positions of call instructions in current block.
    std::unordered_set<uint16_t>
        protectedOperandGPR_; ///< Current-instruction GPR operands that must not be evicted.
    std::unordered_set<uint16_t>
        protectedOperandFPR_; ///< Current-instruction FPR operands that must not be evicted.
    std::unordered_set<PhysReg>
        protectedPhysGPR_; ///< Explicit physical GPR operands that scratch/spills must not clobber.
    std::unordered_set<PhysReg>
        protectedPhysFPR_; ///< Explicit physical FPR operands that scratch/spills must not clobber.

    // CFG + liveness (extracted to shared-solver-backed LivenessAnalysis).
    LivenessAnalysis liveness_;

    // Cross-block register persistence: exit-state cache.

    /// @brief Snapshot of the vreg→physical register mapping at the end of one basic block,
    ///        used to seed the allocation state at single-predecessor successors.
    struct BlockExitState {
        std::unordered_map<uint16_t, PhysReg> gpr; ///< GPR vreg → physical register map.
        std::unordered_map<uint16_t, PhysReg> fpr; ///< FPR vreg → physical register map.
    };

    std::vector<BlockExitState> blockExitStates_;

    // Set when the previous instruction was a call to rt_arr_obj_get
    bool pendingGetBarrier_{false};

    /// Argument registers removed from the free pools because call marshalling
    /// has already written them; returned to the pools after the next call.
    std::vector<PhysReg> reservedForCall_;

    // ---- Cross-block ----
    /// @brief Seed the current block's register state from the single predecessor's exit state.
    void restoreFromPredecessor(std::size_t bi);

    // ---- Physical-register clobbers ----
    /// @brief Evict any vreg resident in a physical register that @p ins defines,
    ///        spilling it first when its value is still needed. Argument registers
    ///        written here are additionally reserved until the next call.
    void evictPhysDefClobbers(MInstr &ins, std::vector<MInstr> &prefix);
    /// @brief Return argument registers reserved during call marshalling to the pools.
    void releaseCallReserved();

    // ---- Next-use analysis ----
    /// @brief Build per-vreg use-position maps for @p bb to guide eviction decisions.
    void computeNextUses(const MBasicBlock &bb);
    /// @brief Return true if @p vreg has a use after the next call in the block.
    bool nextUseAfterCall(uint16_t vreg, RegClass cls) const;
    /// @brief Return the instruction-distance to the next use of @p vreg, or UINT_MAX.
    unsigned getNextUseDistance(uint16_t vreg, RegClass cls) const;
    /// @brief Compute the spill slot last-use index for @p vreg (used to size spill coverage).
    unsigned computeSpillLastUse(uint16_t vreg, RegClass cls, bool forceLiveOut = false) const;
    /// @brief Allocate or retrieve the current spill slot for @p vreg and return its FP offset.
    int ensureCurrentSpillSlot(uint16_t vreg, RegClass cls, bool forceLiveOut = false);
    /// @brief Return true if @p vreg appears as an operand in the current instruction.
    [[nodiscard]] bool isProtectedOperand(uint16_t vreg, RegClass cls) const;
    /// @brief Return true if @p phys appears as an explicit physical operand in the current
    /// instruction.
    [[nodiscard]] bool isProtectedPhys(PhysReg phys, RegClass cls) const;
    /// @brief Return true if @p vreg is live at the exit of the current block.
    [[nodiscard]] bool isLiveOut(uint16_t vreg, RegClass cls) const;

    // ---- Spilling ----
    /// @brief Evict @p id from its physical register and emit a store to its spill slot.
    void spillVictim(RegClass cls, uint16_t id, std::vector<MInstr> &prefix);
    /// @brief Select the LRU-allocated vreg as eviction candidate for @p cls.
    uint16_t selectLRUVictim(RegClass cls);
    /// @brief Select the vreg with the furthest next use as eviction candidate for @p cls.
    uint16_t selectFurthestVictim(RegClass cls);
    /// @brief Proactively evict a vreg when register pressure exceeds available pools.
    void maybeSpillForPressure(RegClass cls, std::vector<MInstr> &prefix);

    // ---- Register materialization ----
    /// @brief Ensure @p r (a virtual register) is backed by a physical register, inserting
    ///        loads/stores into @p prefix / @p suffix as needed.
    void materialize(MReg &r,
                     bool isUse,
                     bool isDef,
                     std::vector<MInstr> &prefix,
                     std::vector<MInstr> &suffix,
                     std::vector<PhysReg> &scratch);
    /// @brief Handle a spilled operand: reload from its slot before use, store after def.
    void handleSpilledOperand(MReg &r,
                              bool fprClass,
                              bool isUse,
                              bool isDef,
                              std::vector<MInstr> &prefix,
                              std::vector<MInstr> &suffix,
                              std::vector<PhysReg> &scratch);
    /// @brief Assign a fresh physical register to virtual register @p vregId.
    void assignNewPhysReg(VState &st, uint16_t vregId, bool fprClass);
    /// @brief Record that @p pr (a callee-saved register) has been used in this function.
    void trackCalleeSavedPhys(PhysReg pr);
    /// @brief Return true if @p pr is callee-saved under AAPCS64 for the given @p cls.
    [[nodiscard]] bool isCalleeSaved(PhysReg pr, RegClass cls) const noexcept;

    // ---- Block / instruction allocation ----
    /// @brief Allocate all instructions in @p bb, replacing vregs with physical registers.
    void allocateBlock(MBasicBlock &bb);
    /// @brief Spill caller-saved physical registers around the call in @p ins.
    void handleCall(MInstr &ins, std::vector<MInstr> &rewritten);
    /// @brief Retire a virtual call operand whose assigned register is clobbered by the call.
    void retireCallOperandAfterCall(uint16_t vreg, RegClass cls);
    /// @brief Materialize all operands of @p ins and append to @p rewritten.
    void allocateInstruction(MInstr &ins, std::vector<MInstr> &rewritten);

    // ---- Cleanup ----
    /// @brief Return scratch physical registers acquired during instruction allocation.
    void releaseScratch(std::vector<PhysReg> &scratch);
    /// @brief Clear per-block allocation state (vreg→phys maps, call positions, etc.).
    void releaseBlockState();
    /// @brief Propagate callee-saved register usage into the FrameBuilder.
    void recordCalleeSavedUsage();
};

} // namespace viper::codegen::aarch64::ra
