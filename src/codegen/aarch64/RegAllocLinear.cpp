//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RegAllocLinear.cpp
/// @brief Linear-scan register allocator for AArch64 Machine IR.
///
/// This file implements a linear-scan register allocator that transforms MIR
/// with virtual registers into MIR with physical AArch64 registers. The
/// allocator handles register assignment, spill code insertion, and tracks
/// callee-saved register usage for prologue/epilogue generation.
///
/// **What is Register Allocation?**
/// Register allocation assigns physical registers to virtual registers in
/// the MIR. When there aren't enough physical registers, values are "spilled"
/// to stack memory and reloaded when needed.
///
/// **Linear-Scan Algorithm Overview:**
/// Linear-scan is a fast O(n log n) register allocation algorithm:
/// 1. Compute live intervals for each virtual register
/// 2. Sort intervals by start position
/// 3. Process intervals in order, assigning registers or spilling
///
/// ```
/// Live intervals:
///   v0: ─────────────
///   v1:     ───────
///   v2:         ─────────
///   v3:             ─────
///
/// Physical registers: x0, x1 (2 available)
///
/// Assignment:
///   Instruction 0: v0 gets x0
///   Instruction 2: v1 gets x1 (x0 still in use by v0)
///   Instruction 4: v0 ends, v2 gets x0
///   Instruction 5: v1 ends, v3 gets x1
/// ```
///
/// **Register Classes:**
/// The allocator handles two register classes independently:
/// | Class | Available Registers          | Used For           |
/// |-------|-----------------------------|--------------------|
/// | GPR   | x0-x28 (except reserved)    | Integers, pointers |
/// | FPR   | d0-d31                      | Floating-point     |
///
/// **Reserved Registers:**
/// | Register | Purpose                    |
/// |----------|----------------------------|
/// | x29 (fp) | Frame pointer              |
/// | x30 (lr) | Link register              |
/// | sp       | Stack pointer              |
/// | x18      | Platform register (Darwin) |
/// | x9       | Global scratch register    |
///
/// **Spill Slot Layout:**
/// ```
/// High addresses
/// ┌────────────────────────────────┐
/// │ Caller's frame                 │
/// ├────────────────────────────────┤ ← Old SP
/// │ Return address (x30)           │
/// │ Saved frame pointer (x29)      │
/// ├────────────────────────────────┤ ← FP (x29)
/// │ Callee-saved registers         │
/// │ Local variables (allocas)      │
/// │ Spill slots for vregs          │  ← Allocated by this pass
/// ├────────────────────────────────┤ ← SP
/// Low addresses
/// ```
///
/// **Callee-Saved Handling:**
/// Registers x19-x28 and d8-d15 are callee-saved per AAPCS64. When the
/// allocator assigns a callee-saved register, it records it in the frame
/// plan for save/restore in prologue/epilogue.
///
/// **Instruction Processing:**
/// For each instruction, the allocator:
/// 1. Identifies operands that are uses (inputs) vs defs (outputs)
/// 2. Ensures used vregs are in physical registers (reload if spilled)
/// 3. Assigns physical registers to defined vregs
/// 4. Frees registers for vregs whose live ranges end
///
/// **Example Transformation:**
/// ```
/// Before (virtual registers):
///   v0 = MovRI #42
///   v1 = AddRRR v0, v0
///   Bl print_i64          ; uses v1 in x0
///
/// After (physical registers):
///   x19 = MovRI #42       ; callee-saved for longer live range
///   x0 = AddRRR x19, x19
///   Bl print_i64
/// ```
///
/// **Spill Example:**
/// ```
/// Before:
///   v0 = ...              ; live across call
///   Bl some_function      ; clobbers caller-saved registers
///   ... = v0              ; need v0 after call
///
/// After:
///   x0 = ...
///   StrRegFpImm x0, [fp, #-24]  ; spill before call
///   Bl some_function
///   LdrRegFpImm x0, [fp, #-24]  ; reload after call
///   ... = x0
/// ```
///
/// @see MachineIR.hpp For MIR type definitions
/// @see FrameBuilder.hpp For stack frame layout
/// @see TargetAArch64.hpp For register definitions
///
//===----------------------------------------------------------------------===//

#include "RegAllocLinear.hpp"

#include "FrameBuilder.hpp"
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64
{
namespace
{

//-----------------------------------------------------------------------------
// Helper predicates for register classification
//-----------------------------------------------------------------------------

static bool isAllocatableGPR(PhysReg r)
{
    switch (r)
    {
        case PhysReg::X29:
        case PhysReg::X30:
        case PhysReg::SP:
        case PhysReg::X18:
        // Reserve the global scratch register so the allocator never hands it out
        case PhysReg::X9:
            return false;
        default:
            return isGPR(r);
    }
}

static bool isArgRegister(PhysReg r, const TargetInfo &ti)
{
    for (auto ar : ti.intArgOrder)
        if (ar == r)
            return true;
    for (auto ar : ti.f64ArgOrder)
        if (ar == r)
            return true;
    return false;
}

//-----------------------------------------------------------------------------
// Opcode classification helpers
//-----------------------------------------------------------------------------

static bool isUseDefDefLike(MOpcode opc)
{
    switch (opc)
    {
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
            return true;
        default:
            return false;
    }
}

static bool isUseDefImmLike(MOpcode opc)
{
    switch (opc)
    {
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
            return true;
        default:
            return false;
    }
}

static bool isSpAdj(MOpcode opc)
{
    return opc == MOpcode::SubSpImm || opc == MOpcode::AddSpImm;
}

static bool isBranch(MOpcode opc)
{
    // Note: Cbz has a register operand that needs allocation, so it's NOT included here
    return opc == MOpcode::Br || opc == MOpcode::BCond;
}

static bool isCall(MOpcode opc)
{
    return opc == MOpcode::Bl;
}

static bool isCmpRR(MOpcode opc)
{
    return opc == MOpcode::CmpRR;
}

static bool isCmpRI(MOpcode opc)
{
    return opc == MOpcode::CmpRI;
}

static bool isCset(MOpcode opc)
{
    return opc == MOpcode::Cset;
}

static bool isRet(MOpcode opc)
{
    return opc == MOpcode::Ret;
}

static bool isTerminator(MOpcode opc)
{
    return isBranch(opc) || isRet(opc);
}

static bool isMemLd(MOpcode opc)
{
    return opc == MOpcode::LdrRegFpImm || opc == MOpcode::LdrRegBaseImm;
}

static bool isMemSt(MOpcode opc)
{
    return opc == MOpcode::StrRegFpImm || opc == MOpcode::StrRegBaseImm ||
           opc == MOpcode::StrRegSpImm;
}

//-----------------------------------------------------------------------------
// Virtual register state tracking
//-----------------------------------------------------------------------------

/// @brief Tracks the allocation state of a virtual register.
///
/// Maintains whether the vreg is currently in a physical register,
/// has been spilled to the stack, and when it was last used (for
/// victim selection heuristics).
struct VState
{
    bool hasPhys{false};       ///< True if currently in a physical register.
    PhysReg phys{PhysReg::X0}; ///< Physical register (valid when hasPhys).
    bool spilled{false};       ///< True if value is on the stack.
    int fpOffset{0};           ///< FP-relative offset of spill slot.
    unsigned lastUse{0};       ///< Instruction index of last use (for LRU).
    unsigned nextUse{UINT_MAX}; ///< Instruction index of next use (for furthest-end-point).
};

//-----------------------------------------------------------------------------
// Register pool management
//-----------------------------------------------------------------------------

struct RegPools
{
    std::deque<PhysReg> gprFree{};
    std::deque<PhysReg> fprFree{};
    std::unordered_set<PhysReg> calleeUsed{};
    std::unordered_set<PhysReg> calleeUsedFPR{};

    void build(const TargetInfo &ti)
    {
        gprFree.clear();
        fprFree.clear();

        // Prefer caller-saved first, exclude argument registers
        for (auto r : ti.callerSavedGPR)
        {
            if (isAllocatableGPR(r) && !isArgRegister(r, ti))
                gprFree.push_back(r);
        }
        for (auto r : ti.calleeSavedGPR)
        {
            if (isAllocatableGPR(r))
                gprFree.push_back(r);
        }

        // FPR: also exclude argument registers V0-V7
        for (auto r : ti.callerSavedFPR)
        {
            if (!isArgRegister(r, ti))
                fprFree.push_back(r);
        }
        for (auto r : ti.calleeSavedFPR)
        {
            fprFree.push_back(r);
        }
    }

    PhysReg takeGPR()
    {
        // Allocator is responsible for ensuring pressure is handled before requesting.
        // Do not ever return the global scratch register here.
        if (gprFree.empty())
        {
            // As a last resort, prefer a callee-saved slot that is not the scratch.
            // This path should be unreachable when maybeSpillForPressure is used correctly.
            // Choose X19 which is callee-saved and never used as global scratch.
            return PhysReg::X19;
        }
        auto r = gprFree.front();
        gprFree.pop_front();
        return r;
    }

    void releaseGPR(PhysReg r)
    {
        gprFree.push_back(r);
    }

    PhysReg takeFPR()
    {
        if (fprFree.empty())
            return kScratchFPR; // Fallback to scratch register when pool exhausted
        auto r = fprFree.front();
        fprFree.pop_front();
        return r;
    }

    void releaseFPR(PhysReg r)
    {
        fprFree.push_back(r);
    }
};

//-----------------------------------------------------------------------------
// Instruction builders
//-----------------------------------------------------------------------------

static MInstr makeMovRR(PhysReg dst, PhysReg src)
{
    return MInstr{MOpcode::MovRR, {MOperand::regOp(dst), MOperand::regOp(src)}};
}

static MInstr makeLdrFp(PhysReg dst, int offset)
{
    return MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(dst), MOperand::immOp(offset)}};
}

static MInstr makeStrFp(PhysReg src, int offset)
{
    return MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(src), MOperand::immOp(offset)}};
}

//-----------------------------------------------------------------------------
// Linear scan register allocator
//-----------------------------------------------------------------------------

class LinearAllocator
{
  public:
    LinearAllocator(MFunction &fn, const TargetInfo &ti) : fn_(fn), ti_(ti), fb_(fn)
    {
        pools_.build(ti);
        buildCFG();
        computeLiveOutSets();
    }

    AllocationResult run()
    {
        for (auto &bb : fn_.blocks)
        {
            allocateBlock(bb);
            releaseBlockState();
        }

        fb_.finalize();
        recordCalleeSavedUsage();

        return AllocationResult{};
    }

  private:
    MFunction &fn_;
    const TargetInfo &ti_;
    FrameBuilder fb_;
    RegPools pools_;
    std::unordered_map<uint16_t, VState> gprStates_;
    std::unordered_map<uint16_t, VState> fprStates_;
    unsigned currentInstrIdx_{0}; ///< Current instruction index for LRU tracking.
    std::unordered_map<uint16_t, unsigned> nextUseGPR_; ///< Next use index for GPR vregs.
    std::unordered_map<uint16_t, unsigned> nextUseFPR_; ///< Next use index for FPR vregs.
    // CFG + liveness (conservative cross-block)
    std::unordered_map<std::string, std::size_t> blockIndex_;
    std::vector<std::vector<std::size_t>> succs_;
    std::vector<std::unordered_set<uint16_t>> liveOutGPR_;
    std::vector<std::unordered_set<uint16_t>> liveOutFPR_;

    void buildCFG()
    {
        blockIndex_.clear();
        succs_.clear();
        succs_.resize(fn_.blocks.size());
        for (std::size_t i = 0; i < fn_.blocks.size(); ++i)
            blockIndex_[fn_.blocks[i].name] = i;
        for (std::size_t i = 0; i < fn_.blocks.size(); ++i)
        {
            const auto &bb = fn_.blocks[i];
            for (const auto &mi : bb.instrs)
            {
                if (mi.opc == MOpcode::Br)
                {
                    if (!mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Label)
                    {
                        auto it = blockIndex_.find(mi.ops[0].label);
                        if (it != blockIndex_.end())
                            succs_[i].push_back(it->second);
                    }
                }
                else if (mi.opc == MOpcode::BCond)
                {
                    if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label)
                    {
                        auto it = blockIndex_.find(mi.ops[1].label);
                        if (it != blockIndex_.end())
                            succs_[i].push_back(it->second);
                    }
                }
                else if (mi.opc == MOpcode::Cbz)
                {
                    // cbz reg, label - label is ops[1]
                    if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label)
                    {
                        auto it = blockIndex_.find(mi.ops[1].label);
                        if (it != blockIndex_.end())
                            succs_[i].push_back(it->second);
                    }
                }
            }
        }
    }

    void computeLiveOutSets()
    {
        liveOutGPR_.assign(fn_.blocks.size(), {});
        liveOutFPR_.assign(fn_.blocks.size(), {});
        for (std::size_t i = 0; i < fn_.blocks.size(); ++i)
        {
            for (auto sidx : succs_[i])
            {
                const auto &succ = fn_.blocks[sidx];
                for (const auto &mi : succ.instrs)
                {
                    for (const auto &op : mi.ops)
                    {
                        if (op.kind == MOperand::Kind::Reg && !op.reg.isPhys)
                        {
                            if (op.reg.cls == RegClass::GPR)
                                liveOutGPR_[i].insert(op.reg.idOrPhys);
                            else
                                liveOutFPR_[i].insert(op.reg.idOrPhys);
                        }
                    }
                }
            }
        }
    }

    /// @brief Compute next-use distances for all vregs in a block.
    ///
    /// This enables "furthest end point" spilling: when we need to spill,
    /// we choose the vreg that won't be needed for the longest time.
    /// Scanning backward from the end of the block, we record when each vreg
    /// is next used.
    void computeNextUses(const MBasicBlock &bb)
    {
        nextUseGPR_.clear();
        nextUseFPR_.clear();

        // Scan backward through the block
        unsigned idx = static_cast<unsigned>(bb.instrs.size());
        for (auto it = bb.instrs.rbegin(); it != bb.instrs.rend(); ++it)
        {
            --idx;
            for (const auto &op : it->ops)
            {
                if (op.kind != MOperand::Kind::Reg || op.reg.isPhys)
                    continue;
                // Record this as the next use (scanning backward means first occurrence
                // we see is the furthest use from the start)
                if (op.reg.cls == RegClass::GPR)
                    nextUseGPR_[op.reg.idOrPhys] = idx;
                else
                    nextUseFPR_[op.reg.idOrPhys] = idx;
            }
        }
    }

    /// @brief Get the next use distance for a vreg from current position.
    /// @return Distance to next use, or UINT_MAX if not used again in block.
    unsigned getNextUseDistance(uint16_t vreg, RegClass cls) const
    {
        const auto &map = (cls == RegClass::GPR) ? nextUseGPR_ : nextUseFPR_;
        auto it = map.find(vreg);
        if (it == map.end())
            return UINT_MAX; // Not used again in this block
        if (it->second <= currentInstrIdx_)
            return UINT_MAX; // Already past this use
        return it->second - currentInstrIdx_;
    }

    //-------------------------------------------------------------------------
    // Spilling
    //-------------------------------------------------------------------------

    void spillVictim(RegClass cls, uint16_t id, std::vector<MInstr> &prefix)
    {
        auto &st = (cls == RegClass::GPR) ? gprStates_[id] : fprStates_[id];
        if (!st.hasPhys)
            return;

        const int off = fb_.ensureSpill(id);
        if (cls == RegClass::GPR)
        {
            prefix.push_back(makeStrFp(st.phys, off));
            pools_.releaseGPR(st.phys);
        }
        else
        {
            prefix.push_back(
                MInstr{MOpcode::StrFprFpImm, {MOperand::regOp(st.phys), MOperand::immOp(off)}});
            pools_.releaseFPR(st.phys);
        }
        st.hasPhys = false;
        st.spilled = true;
    }

    /// @brief Selects a victim to spill using furthest-end-point heuristic.
    ///
    /// When register pressure requires spilling, we choose the virtual register
    /// whose value won't be needed for the longest time. This is the classic
    /// linear-scan spill heuristic and generally produces better code than LRU.
    ///
    /// @param cls Register class to spill from.
    /// @return The vreg ID of the victim, or UINT16_MAX if none found.
    uint16_t selectFurthestVictim(RegClass cls)
    {
        auto &states = (cls == RegClass::GPR) ? gprStates_ : fprStates_;
        uint16_t victim = UINT16_MAX;
        unsigned furthestUse = 0;

        for (auto &kv : states)
        {
            if (!kv.second.hasPhys)
                continue;

            unsigned nextUse = getNextUseDistance(kv.first, cls);
            // Prefer to spill the one with furthest (or no) next use
            if (nextUse > furthestUse)
            {
                furthestUse = nextUse;
                victim = kv.first;
            }
        }
        return victim;
    }

    void maybeSpillForPressure(RegClass cls, std::vector<MInstr> &prefix)
    {
        if (cls == RegClass::GPR)
        {
            if (!pools_.gprFree.empty())
                return;
            // Use furthest-end-point heuristic to select victim
            uint16_t victim = selectFurthestVictim(RegClass::GPR);
            if (victim != UINT16_MAX)
                spillVictim(RegClass::GPR, victim, prefix);
        }
        else
        {
            if (!pools_.fprFree.empty())
                return;
            // Use furthest-end-point heuristic to select victim
            uint16_t victim = selectFurthestVictim(RegClass::FPR);
            if (victim != UINT16_MAX)
                spillVictim(RegClass::FPR, victim, prefix);
        }
    }

    //-------------------------------------------------------------------------
    // Register materialization
    //-------------------------------------------------------------------------

    void materialize(MReg &r,
                     bool isUse,
                     bool isDef,
                     std::vector<MInstr> &prefix,
                     std::vector<MInstr> &suffix,
                     std::vector<PhysReg> &scratch)
    {
        if (r.isPhys)
        {
            trackCalleeSavedPhys(static_cast<PhysReg>(r.idOrPhys));
            return;
        }

        const bool isFPR = (r.cls == RegClass::FPR);
        auto &st = isFPR ? fprStates_[r.idOrPhys] : gprStates_[r.idOrPhys];

        // Update last use for LRU tracking (use or def counts as access)
        if (isUse || isDef)
            st.lastUse = currentInstrIdx_;

        if (st.spilled)
        {
            handleSpilledOperand(r, isFPR, isUse, isDef, prefix, suffix, scratch);
            return;
        }

        if (!st.hasPhys)
        {
            assignNewPhysReg(st, isFPR);
        }

        r.isPhys = true;
        r.idOrPhys = static_cast<uint16_t>(st.phys);
    }

    void handleSpilledOperand(MReg &r,
                              bool isFPR,
                              bool isUse,
                              bool isDef,
                              std::vector<MInstr> &prefix,
                              std::vector<MInstr> &suffix,
                              std::vector<PhysReg> &scratch)
    {
        PhysReg tmp = isFPR ? pools_.takeFPR() : pools_.takeGPR();
        const int off = fb_.ensureSpill(r.idOrPhys);

        if (isUse)
        {
            if (isFPR)
                prefix.push_back(
                    MInstr{MOpcode::LdrFprFpImm, {MOperand::regOp(tmp), MOperand::immOp(off)}});
            else
                prefix.push_back(makeLdrFp(tmp, off));
        }

        if (isDef)
        {
            if (isFPR)
                suffix.push_back(
                    MInstr{MOpcode::StrFprFpImm, {MOperand::regOp(tmp), MOperand::immOp(off)}});
            else
                suffix.push_back(makeStrFp(tmp, off));
        }

        r.isPhys = true;
        r.idOrPhys = static_cast<uint16_t>(tmp);
        scratch.push_back(tmp);
    }

    void assignNewPhysReg(VState &st, bool isFPR)
    {
        PhysReg phys = isFPR ? pools_.takeFPR() : pools_.takeGPR();
        st.hasPhys = true;
        st.phys = phys;

        if (!isFPR)
        {
            if (std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), phys) !=
                ti_.calleeSavedGPR.end())
            {
                pools_.calleeUsed.insert(phys);
            }
        }
        else
        {
            if (std::find(ti_.calleeSavedFPR.begin(), ti_.calleeSavedFPR.end(), phys) !=
                ti_.calleeSavedFPR.end())
            {
                pools_.calleeUsedFPR.insert(phys);
            }
        }
    }

    void trackCalleeSavedPhys(PhysReg pr)
    {
        if (isGPR(pr))
        {
            if (std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), pr) !=
                ti_.calleeSavedGPR.end())
            {
                pools_.calleeUsed.insert(pr);
            }
        }
    }

    /// @brief Check if a physical register is callee-saved.
    ///
    /// Callee-saved registers (x19-x28, d8-d15) don't need to be spilled around
    /// calls because the callee is required to preserve them.
    [[nodiscard]] bool isCalleeSaved(PhysReg pr, RegClass cls) const noexcept
    {
        if (cls == RegClass::GPR)
        {
            return std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), pr) !=
                   ti_.calleeSavedGPR.end();
        }
        else
        {
            return std::find(ti_.calleeSavedFPR.begin(), ti_.calleeSavedFPR.end(), pr) !=
                   ti_.calleeSavedFPR.end();
        }
    }

    //-------------------------------------------------------------------------
    // Operand role classification
    //-------------------------------------------------------------------------

    std::pair<bool, bool> operandRoles(const MInstr &ins, std::size_t idx)
    {
        // Returns {isUse, isDef}
        switch (ins.opc)
        {
            case MOpcode::MovRR:
                return {idx == 1, idx == 0};
            case MOpcode::MovRI:
                return {false, idx == 0};
            case MOpcode::FMovRR:
                return {idx == 1, idx == 0};
            case MOpcode::FMovRI:
                return {false, idx == 0};
            default:
                break;
        }

        if (isUseDefDefLike(ins.opc))
        {
            if (idx == 0)
                return {true, true};
            if (idx == 1 || idx == 2)
                return {true, false};
        }

        // FP RRR behave like integer RRR
        if (ins.opc == MOpcode::FAddRRR || ins.opc == MOpcode::FSubRRR ||
            ins.opc == MOpcode::FMulRRR || ins.opc == MOpcode::FDivRRR)
        {
            if (idx == 0)
                return {true, true};
            if (idx == 1 || idx == 2)
                return {true, false};
        }

        if (isUseDefImmLike(ins.opc))
        {
            if (idx == 0)
                return {true, true};
            if (idx == 1)
                return {true, false};
        }

        if (ins.opc == MOpcode::SCvtF || ins.opc == MOpcode::FCvtZS || ins.opc == MOpcode::UCvtF ||
            ins.opc == MOpcode::FCvtZU)
        {
            return {idx == 1, idx == 0};
        }

        if (isCmpRR(ins.opc) || ins.opc == MOpcode::FCmpRR)
            return {true, false};

        if (isCmpRI(ins.opc) && idx == 0)
            return {true, false};

        if (isCset(ins.opc))
            return {false, idx == 0};

        if (isMemLd(ins.opc) && idx == 0)
            return {false, true};

        if (isMemSt(ins.opc) && idx == 0)
            return {true, false};

        if (ins.opc == MOpcode::LdrFprFpImm)
            return {false, idx == 0};

        if ((ins.opc == MOpcode::StrFprFpImm || ins.opc == MOpcode::StrFprSpImm) && idx == 0)
            return {true, false};

        // AdrPage: dst is def-only, label operand is not a register
        if (ins.opc == MOpcode::AdrPage)
            return {false, idx == 0};

        // AddPageOff: dst is def+use (same reg for src and dst), label operand is not a register
        if (ins.opc == MOpcode::AddPageOff)
        {
            if (idx == 0)
                return {false, true}; // dst is def-only
            if (idx == 1)
                return {true, false}; // src is use
            return {false, false};    // label is neither
        }

        // MSubRRRR: msub dst, mul1, mul2, sub => dst = sub - mul1*mul2
        // Operands: [0]=dst (def), [1]=mul1 (use), [2]=mul2 (use), [3]=sub (use)
        if (ins.opc == MOpcode::MSubRRRR)
        {
            if (idx == 0)
                return {false, true}; // dst is def-only
            return {true, false};     // all others are use-only
        }

        // Cbz: cbz reg, label => branch if reg is zero
        // Operands: [0]=reg (use), [1]=label
        if (ins.opc == MOpcode::Cbz)
        {
            if (idx == 0)
                return {true, false}; // reg is use
            return {false, false};    // label is neither
        }

        return {true, false};
    }

    //-------------------------------------------------------------------------
    // Block allocation
    //-------------------------------------------------------------------------

    void allocateBlock(MBasicBlock &bb)
    {
        // Compute next-use information for furthest-end-point spilling
        computeNextUses(bb);
        currentInstrIdx_ = 0;

        std::vector<MInstr> rewritten;
        rewritten.reserve(bb.instrs.size());

        for (auto &ins : bb.instrs)
        {
            if (isSpAdj(ins.opc) || isBranch(ins.opc))
            {
                rewritten.push_back(ins);
                ++currentInstrIdx_;
                continue;
            }

            if (isCall(ins.opc))
            {
                handleCall(ins, rewritten);
                ++currentInstrIdx_;
                continue;
            }

            allocateInstruction(ins, rewritten);
            ++currentInstrIdx_;
        }

        if (!rewritten.empty())
        {
            std::vector<MInstr> endSpills;
            auto itB = blockIndex_.find(bb.name);
            if (itB != blockIndex_.end())
            {
                const std::size_t bi = itB->second;
                for (auto vid : liveOutGPR_[bi])
                {
                    auto it = gprStates_.find(vid);
                    if (it != gprStates_.end() && it->second.hasPhys)
                    {
                        const int off = fb_.ensureSpill(vid);
                        endSpills.push_back(makeStrFp(it->second.phys, off));
                        pools_.releaseGPR(it->second.phys);
                        it->second.hasPhys = false;
                        it->second.spilled = true;
                    }
                }
                for (auto vid : liveOutFPR_[bi])
                {
                    auto it = fprStates_.find(vid);
                    if (it != fprStates_.end() && it->second.hasPhys)
                    {
                        const int off = fb_.ensureSpill(vid);
                        endSpills.push_back(
                            MInstr{MOpcode::StrFprFpImm,
                                   {MOperand::regOp(it->second.phys), MOperand::immOp(off)}});
                        pools_.releaseFPR(it->second.phys);
                        it->second.hasPhys = false;
                        it->second.spilled = true;
                    }
                }
            }
            if (!endSpills.empty())
            {
                std::size_t insertPos = rewritten.size();
                for (std::size_t i = rewritten.size(); i-- > 0;)
                {
                    if (isTerminator(rewritten[i].opc))
                    {
                        insertPos = i;
                        break;
                    }
                }
                rewritten.insert(rewritten.begin() + static_cast<long>(insertPos),
                                 endSpills.begin(),
                                 endSpills.end());
            }
        }
        bb.instrs = std::move(rewritten);
    }

    void handleCall(MInstr &ins, std::vector<MInstr> &rewritten)
    {
        bool isArrayObjGet = false;
        if (!ins.ops.empty() && ins.ops[0].kind == MOperand::Kind::Label)
        {
            if (ins.ops[0].label == "rt_arr_obj_get")
                isArrayObjGet = true;
        }
        // Spill only caller-saved registers before the call.
        // Callee-saved registers (x19-x28, d8-d15) don't need spilling because
        // the callee is required to preserve them.
        std::vector<MInstr> preCall;
        for (auto &kv : gprStates_)
        {
            if (kv.second.hasPhys && !isCalleeSaved(kv.second.phys, RegClass::GPR))
                spillVictim(RegClass::GPR, kv.first, preCall);
        }
        for (auto &kv : fprStates_)
        {
            if (kv.second.hasPhys && !isCalleeSaved(kv.second.phys, RegClass::FPR))
                spillVictim(RegClass::FPR, kv.first, preCall);
        }
        for (auto &mi : preCall)
            rewritten.push_back(std::move(mi));
        rewritten.push_back(ins);
        if (isArrayObjGet)
            pendingGetBarrier_ = true;
    }

    void allocateInstruction(MInstr &ins, std::vector<MInstr> &rewritten)
    {
        std::vector<MInstr> prefix;
        bool applyGetBarrier = false;
        uint16_t getBarrierDstVreg = 0;
        if (pendingGetBarrier_ && ins.opc == MOpcode::MovRR && ins.ops.size() >= 2)
        {
            const auto &dst = ins.ops[0];
            const auto &src = ins.ops[1];
            if (dst.kind == MOperand::Kind::Reg && !dst.reg.isPhys &&
                src.kind == MOperand::Kind::Reg && src.reg.isPhys &&
                static_cast<PhysReg>(src.reg.idOrPhys) == PhysReg::X0)
            {
                applyGetBarrier = true;
                getBarrierDstVreg = dst.reg.idOrPhys;
                pendingGetBarrier_ = false;
            }
            else
            {
                pendingGetBarrier_ = false;
            }
        }
        std::vector<MInstr> suffix;
        std::vector<PhysReg> scratch;

        // Rewrite operands
        for (std::size_t i = 0; i < ins.ops.size(); ++i)
        {
            auto &op = ins.ops[i];
            auto [isUse, isDef] = operandRoles(ins, i);
            if (op.kind == MOperand::Kind::Reg)
            {
                maybeSpillForPressure(op.reg.cls, prefix);
                materialize(op.reg, isUse, isDef, prefix, suffix, scratch);
            }
        }

        // Apply post-call barrier for rt_arr_obj_get
        if (applyGetBarrier)
        {
            auto it = gprStates_.find(getBarrierDstVreg);
            if (it != gprStates_.end() && it->second.hasPhys)
            {
                const int off = fb_.ensureSpill(getBarrierDstVreg);
                suffix.push_back(makeStrFp(it->second.phys, off));
                pools_.releaseGPR(it->second.phys);
                it->second.hasPhys = false;
                it->second.spilled = true;
            }
        }

        // Emit
        for (auto &pre : prefix)
            rewritten.push_back(std::move(pre));
        rewritten.push_back(std::move(ins));
        for (auto &suf : suffix)
            rewritten.push_back(std::move(suf));

        // Release scratch registers
        releaseScratch(scratch);

        // Advance instruction counter for LRU tracking
        ++currentInstrIdx_;
    }

    void releaseScratch(std::vector<PhysReg> &scratch)
    {
        for (auto pr : scratch)
        {
            if (isGPR(pr))
            {
                if (std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), pr) !=
                    ti_.calleeSavedGPR.end())
                    pools_.calleeUsed.insert(pr);
                pools_.releaseGPR(pr);
            }
            else
            {
                if (std::find(ti_.calleeSavedFPR.begin(), ti_.calleeSavedFPR.end(), pr) !=
                    ti_.calleeSavedFPR.end())
                    pools_.calleeUsedFPR.insert(pr);
                pools_.releaseFPR(pr);
            }
        }
    }

    void releaseBlockState()
    {
        for (auto &kv : gprStates_)
        {
            if (kv.second.hasPhys)
            {
                pools_.releaseGPR(kv.second.phys);
                kv.second.hasPhys = false;
            }
        }
        for (auto &kv : fprStates_)
        {
            if (kv.second.hasPhys)
            {
                pools_.releaseFPR(kv.second.phys);
                kv.second.hasPhys = false;
            }
        }
    }

    void recordCalleeSavedUsage()
    {
        if (!pools_.calleeUsed.empty())
        {
            for (auto r : ti_.calleeSavedGPR)
            {
                if (pools_.calleeUsed.contains(r))
                    fn_.savedGPRs.push_back(r);
            }
        }
        if (!pools_.calleeUsedFPR.empty())
        {
            for (auto r : ti_.calleeSavedFPR)
            {
                if (pools_.calleeUsedFPR.contains(r))
                    fn_.savedFPRs.push_back(r);
            }
        }
    }

    // Set when the previous instruction was a call to rt_arr_obj_get
    bool pendingGetBarrier_{false};
};

} // namespace

AllocationResult allocate(MFunction &fn, const TargetInfo &ti)
{
    LinearAllocator allocator(fn, ti);
    return allocator.run();
}

} // namespace viper::codegen::aarch64
