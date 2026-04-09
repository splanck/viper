//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/Allocator.hpp
// Purpose: Core linear-scan register allocator class for AArch64. Owns all
//          allocation state and drives the per-block, per-instruction
//          allocation loop.
// Key invariants:
//   - After run(), every MReg in the MFunction has isPhys=true.
//   - Spill slots are allocated via FrameBuilder; callee-saved usage is
//     recorded for prologue/epilogue generation.
//   - Cross-block register persistence uses single-predecessor exit states.
// Ownership/Lifetime:
//   - Constructed per-function; borrows MFunction and TargetInfo references.
//   - Must not outlive the MFunction it modifies.
// Links: docs/codemap.md
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

class LinearAllocator {
  public:
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
    unsigned currentInstrIdx_{0};    ///< Current instruction index for LRU tracking.
    std::size_t currentBlockIdx_{0}; ///< Current block index for liveness lookups.
    unsigned currentBlockInstrCount_{0}; ///< Instruction count of the current block.
    std::unordered_map<uint16_t, std::vector<unsigned>>
        usePositionsGPR_; ///< All use positions for GPR vregs.
    std::unordered_map<uint16_t, std::vector<unsigned>>
        usePositionsFPR_;                 ///< All use positions for FPR vregs.
    std::vector<unsigned> callPositions_; ///< Positions of call instructions in current block.
    std::unordered_set<uint16_t>
        protectedUseGPR_; ///< Current-instruction GPR uses that must not be evicted early.
    std::unordered_set<uint16_t>
        protectedUseFPR_; ///< Current-instruction FPR uses that must not be evicted early.

    // CFG + liveness (extracted to shared-solver-backed LivenessAnalysis).
    LivenessAnalysis liveness_;

    // Cross-block register persistence: exit-state cache.

    struct BlockExitState {
        std::unordered_map<uint16_t, PhysReg> gpr;
        std::unordered_map<uint16_t, PhysReg> fpr;
    };

    std::vector<BlockExitState> blockExitStates_;

    // Set when the previous instruction was a call to rt_arr_obj_get
    bool pendingGetBarrier_{false};

    // ---- Cross-block ----
    void restoreFromPredecessor(std::size_t bi);

    // ---- Next-use analysis ----
    void computeNextUses(const MBasicBlock &bb);
    bool nextUseAfterCall(uint16_t vreg, RegClass cls) const;
    unsigned getNextUseDistance(uint16_t vreg, RegClass cls) const;
    unsigned computeSpillLastUse(uint16_t vreg, RegClass cls, bool forceLiveOut = false) const;
    int ensureCurrentSpillSlot(uint16_t vreg, RegClass cls, bool forceLiveOut = false);
    [[nodiscard]] bool isProtectedUse(uint16_t vreg, RegClass cls) const;
    [[nodiscard]] bool isLiveOut(uint16_t vreg, RegClass cls) const;

    // ---- Spilling ----
    void spillVictim(RegClass cls, uint16_t id, std::vector<MInstr> &prefix);
    uint16_t selectLRUVictim(RegClass cls);
    uint16_t selectFurthestVictim(RegClass cls);
    void maybeSpillForPressure(RegClass cls, std::vector<MInstr> &prefix);

    // ---- Register materialization ----
    void materialize(MReg &r,
                     bool isUse,
                     bool isDef,
                     std::vector<MInstr> &prefix,
                     std::vector<MInstr> &suffix,
                     std::vector<PhysReg> &scratch);
    void handleSpilledOperand(MReg &r,
                              bool isFPR,
                              bool isUse,
                              bool isDef,
                              std::vector<MInstr> &prefix,
                              std::vector<MInstr> &suffix,
                              std::vector<PhysReg> &scratch);
    void assignNewPhysReg(VState &st, uint16_t vregId, bool isFPR);
    void trackCalleeSavedPhys(PhysReg pr);
    [[nodiscard]] bool isCalleeSaved(PhysReg pr, RegClass cls) const noexcept;

    // ---- Block / instruction allocation ----
    void allocateBlock(MBasicBlock &bb);
    void handleCall(MInstr &ins, std::vector<MInstr> &rewritten);
    void allocateInstruction(MInstr &ins, std::vector<MInstr> &rewritten);

    // ---- Cleanup ----
    void releaseScratch(std::vector<PhysReg> &scratch);
    void releaseBlockState();
    void recordCalleeSavedUsage();
};

} // namespace viper::codegen::aarch64::ra
