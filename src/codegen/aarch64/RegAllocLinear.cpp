//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/RegAllocLinear.cpp
// Purpose: Minimal linear-scan allocator for AArch64 Machine IR with virtual
// //        GPRs. Assigns physical registers, inserts simple spill/reload code
// //        using FP-relative slots, and records callee-saved usage.
//
//===----------------------------------------------------------------------===//

#include "RegAllocLinear.hpp"

#include "FrameBuilder.hpp"
#include <algorithm>
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

struct VState
{
    bool hasPhys{false};
    PhysReg phys{PhysReg::X0};
    bool spilled{false};
    int fpOffset{0};
};

//-----------------------------------------------------------------------------
// Register pool management
//-----------------------------------------------------------------------------

struct RegPools
{
    std::vector<PhysReg> gprFree{};
    std::vector<PhysReg> fprFree{};
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
        if (gprFree.empty())
            return PhysReg::X9;
        auto r = gprFree.front();
        gprFree.erase(gprFree.begin());
        return r;
    }

    void releaseGPR(PhysReg r)
    {
        gprFree.push_back(r);
    }

    PhysReg takeFPR()
    {
        if (fprFree.empty())
            return PhysReg::V16;
        auto r = fprFree.front();
        fprFree.erase(fprFree.begin());
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
    LinearAllocator(MFunction &fn, const TargetInfo &ti)
        : fn_(fn), ti_(ti), fb_(fn)
    {
        pools_.build(ti);
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

    void maybeSpillForPressure(RegClass cls, std::vector<MInstr> &prefix)
    {
        if (cls == RegClass::GPR)
        {
            if (!pools_.gprFree.empty())
                return;
            for (auto &kv : gprStates_)
            {
                if (kv.second.hasPhys)
                {
                    spillVictim(RegClass::GPR, kv.first, prefix);
                    break;
                }
            }
        }
        else
        {
            if (!pools_.fprFree.empty())
                return;
            for (auto &kv : fprStates_)
            {
                if (kv.second.hasPhys)
                {
                    spillVictim(RegClass::FPR, kv.first, prefix);
                    break;
                }
            }
        }
    }

    //-------------------------------------------------------------------------
    // Register materialization
    //-------------------------------------------------------------------------

    void materialize(MReg &r, bool isUse, bool isDef,
                     std::vector<MInstr> &prefix, std::vector<MInstr> &suffix,
                     std::vector<PhysReg> &scratch)
    {
        if (r.isPhys)
        {
            trackCalleeSavedPhys(static_cast<PhysReg>(r.idOrPhys));
            return;
        }

        const bool isFPR = (r.cls == RegClass::FPR);
        auto &st = isFPR ? fprStates_[r.idOrPhys] : gprStates_[r.idOrPhys];

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

    void handleSpilledOperand(MReg &r, bool isFPR, bool isUse, bool isDef,
                              std::vector<MInstr> &prefix, std::vector<MInstr> &suffix,
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

        if (ins.opc == MOpcode::SCvtF || ins.opc == MOpcode::FCvtZS ||
            ins.opc == MOpcode::UCvtF || ins.opc == MOpcode::FCvtZU)
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

        return {true, false};
    }

    //-------------------------------------------------------------------------
    // Block allocation
    //-------------------------------------------------------------------------

    void allocateBlock(MBasicBlock &bb)
    {
        std::vector<MInstr> rewritten;
        rewritten.reserve(bb.instrs.size());

        for (auto &ins : bb.instrs)
        {
            if (isSpAdj(ins.opc) || isBranch(ins.opc))
            {
                rewritten.push_back(ins);
                continue;
            }

            if (isCall(ins.opc))
            {
                handleCall(ins, rewritten);
                continue;
            }

            allocateInstruction(ins, rewritten);
        }

        bb.instrs = std::move(rewritten);
    }

    void handleCall(MInstr &ins, std::vector<MInstr> &rewritten)
    {
        std::vector<MInstr> preCall;
        for (auto &kv : gprStates_)
        {
            if (kv.second.hasPhys)
                spillVictim(RegClass::GPR, kv.first, preCall);
        }
        for (auto &kv : fprStates_)
        {
            if (kv.second.hasPhys)
                spillVictim(RegClass::FPR, kv.first, preCall);
        }
        for (auto &mi : preCall)
            rewritten.push_back(std::move(mi));
        rewritten.push_back(ins);
    }

    void allocateInstruction(MInstr &ins, std::vector<MInstr> &rewritten)
    {
        std::vector<MInstr> prefix;
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

        // Emit
        for (auto &pre : prefix)
            rewritten.push_back(std::move(pre));
        rewritten.push_back(std::move(ins));
        for (auto &suf : suffix)
            rewritten.push_back(std::move(suf));

        // Release scratch registers
        releaseScratch(scratch);
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
                if (pools_.calleeUsed.count(r))
                    fn_.savedGPRs.push_back(r);
            }
        }
        if (!pools_.calleeUsedFPR.empty())
        {
            for (auto r : ti_.calleeSavedFPR)
            {
                if (pools_.calleeUsedFPR.count(r))
                    fn_.savedFPRs.push_back(r);
            }
        }
    }
};

} // namespace

AllocationResult allocate(MFunction &fn, const TargetInfo &ti)
{
    LinearAllocator allocator(fn, ti);
    return allocator.run();
}

} // namespace viper::codegen::aarch64
