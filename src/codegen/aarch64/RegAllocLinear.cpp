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

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include "FrameBuilder.hpp"

namespace viper::codegen::aarch64
{
namespace
{

static bool isAllocatableGPR(PhysReg r)
{
    // Exclude FP, LR, SP, reserved X18
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

struct VState
{
    bool hasPhys{false};
    PhysReg phys{PhysReg::X0};
    bool spilled{false};
    int fpOffset{0}; // negative offset from FP
};

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

struct Pools
{
    std::vector<PhysReg> gprFree{};
    std::vector<PhysReg> fprFree{};
    std::unordered_set<PhysReg> calleeUsed{};
    std::unordered_set<PhysReg> calleeUsedFPR{};
};

static void buildPools(const TargetInfo &ti, Pools &out)
{
    out.gprFree.clear();
    out.fprFree.clear();
    // Prefer caller-saved first for temporaries, then callee-saved.
    for (auto r : ti.callerSavedGPR)
    {
        if (isAllocatableGPR(r))
            out.gprFree.push_back(r);
    }
    for (auto r : ti.calleeSavedGPR)
    {
        if (isAllocatableGPR(r))
            out.gprFree.push_back(r);
    }
    for (auto r : ti.callerSavedFPR)
    {
        out.fprFree.push_back(r);
    }
    for (auto r : ti.calleeSavedFPR)
    {
        out.fprFree.push_back(r);
    }
    // Keep a deterministic order.
}

static PhysReg takeGPR(Pools &p)
{
    if (p.gprFree.empty())
    {
        // Fallback to X9 if absolutely necessary (shouldn't happen with spills)
        return PhysReg::X9;
    }
    auto r = p.gprFree.front();
    p.gprFree.erase(p.gprFree.begin());
    return r;
}

static void releaseGPR(Pools &p, PhysReg r)
{
    // Do not reinsert special regs filtered earlier; assume r is allocatable
    p.gprFree.push_back(r);
}

static PhysReg takeFPR(Pools &p)
{
    if (p.fprFree.empty())
    {
        return PhysReg::V16; // fallback
    }
    auto r = p.fprFree.front();
    p.fprFree.erase(p.fprFree.begin());
    return r;
}

static void releaseFPR(Pools &p, PhysReg r)
{
    p.fprFree.push_back(r);
}

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

static bool isCmpRR(MOpcode opc) { return opc == MOpcode::CmpRR; }
static bool isCmpRI(MOpcode opc) { return opc == MOpcode::CmpRI; }
static bool isMovRR(MOpcode opc) { return opc == MOpcode::MovRR; }
static bool isMovRI(MOpcode opc) { return opc == MOpcode::MovRI; }
static bool isCset(MOpcode opc) { return opc == MOpcode::Cset; }
static bool isMemLd(MOpcode opc) { return opc == MOpcode::LdrRegFpImm; }
static bool isMemSt(MOpcode opc) { return opc == MOpcode::StrRegFpImm; }
static bool isSpAdj(MOpcode opc) { return opc == MOpcode::SubSpImm || opc == MOpcode::AddSpImm; }
static bool isBranch(MOpcode opc)
{
    return opc == MOpcode::Br || opc == MOpcode::BCond || opc == MOpcode::Bl;
}

} // namespace

AllocationResult allocate(MFunction &fn, const TargetInfo &ti)
{
    // Map each vreg id → state
    std::unordered_map<uint16_t, VState> states;    // GPR vregs
    std::unordered_map<uint16_t, VState> statesFPR; // FPR vregs
    Pools pools{};
    buildPools(ti, pools);
    FrameBuilder fb{fn};

    auto spillVictim = [&](RegClass cls, uint16_t id, std::vector<MInstr> &prefix) {
        auto &st = (cls == RegClass::GPR) ? states[id] : statesFPR[id];
        if (!st.hasPhys)
            return;
        const int off = fb.ensureSpill(id);
        if (cls == RegClass::GPR)
        {
            prefix.push_back(makeStrFp(st.phys, off));
            releaseGPR(pools, st.phys);
        }
        else
        {
            prefix.push_back(MInstr{MOpcode::StrFprFpImm,
                                    {MOperand::regOp(st.phys), MOperand::immOp(off)}});
            releaseFPR(pools, st.phys);
        }
        st.hasPhys = false;
        st.spilled = true;
    };

    auto materialize = [&](MReg &r, bool isUse, bool isDef, std::vector<MInstr> &prefix,
                           std::vector<MInstr> &suffix, std::vector<PhysReg> &scratch) {
        if (r.isPhys)
        {
            // Track callee-saved usage for frame plan if used as temp.
            auto pr = static_cast<PhysReg>(r.idOrPhys);
            if (isGPR(pr))
            {
                if (std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), pr) !=
                    ti.calleeSavedGPR.end())
                {
                    pools.calleeUsed.insert(pr);
                }
            }
            return; // already physical
        }
        // Only handle GPR vregs in this minimal allocator
        const bool isF = (r.cls == RegClass::FPR);
        auto &st = isF ? statesFPR[r.idOrPhys] : states[r.idOrPhys];
        if (st.spilled)
        {
            // Reload into a scratch for uses; store back for defs
            PhysReg tmp = isF ? takeFPR(pools) : takeGPR(pools);
            const int off = fb.ensureSpill(r.idOrPhys);
            if (isUse)
            {
                if (isF)
                    prefix.push_back(MInstr{MOpcode::LdrFprFpImm,
                                            {MOperand::regOp(tmp), MOperand::immOp(off)}});
                else
                    prefix.push_back(makeLdrFp(tmp, off));
            }
            if (isDef)
            {
                if (isF)
                    suffix.push_back(MInstr{MOpcode::StrFprFpImm,
                                            {MOperand::regOp(tmp), MOperand::immOp(off)}});
                else
                    suffix.push_back(makeStrFp(tmp, off));
            }
            r.isPhys = true;
            r.idOrPhys = static_cast<uint16_t>(tmp);
            scratch.push_back(tmp);
            return;
        }
        if (!st.hasPhys)
        {
            // Assign a new physical register
            PhysReg phys = isF ? takeFPR(pools) : takeGPR(pools);
            st.hasPhys = true;
            st.phys = phys;
            // Record callee-saved usage if taken from that set.
            if (!isF && std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), phys) !=
                            ti.calleeSavedGPR.end())
            {
                pools.calleeUsed.insert(phys);
            }
            if (isF && std::find(ti.calleeSavedFPR.begin(), ti.calleeSavedFPR.end(), phys) !=
                            ti.calleeSavedFPR.end())
            {
                pools.calleeUsedFPR.insert(phys);
            }
        }
        // Rewrite operand to physical
        r.isPhys = true;
        r.idOrPhys = static_cast<uint16_t>(st.phys);
        // Note: we do not expire within the block in this simple pass.
    };

    for (auto &bb : fn.blocks)
    {
        std::vector<MInstr> rewritten;
        rewritten.reserve(bb.instrs.size());

        for (auto &ins : bb.instrs)
        {
            // Skip non-GPR affecting instructions
            if (isSpAdj(ins.opc) || isBranch(ins.opc))
            {
                rewritten.push_back(ins);
                continue;
            }

            std::vector<MInstr> prefix;
            std::vector<MInstr> suffix;
            std::vector<PhysReg> scratch;

            auto rolesFor = [&](std::size_t idx) -> std::pair<bool, bool> {
                // returns {isUse, isDef}
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
                if (ins.opc == MOpcode::SCvtF || ins.opc == MOpcode::FCvtZS)
                {
                    return {idx == 1, idx == 0};
                }
                if (isCmpRR(ins.opc))
                {
                    return {true, false};
                }
                if (ins.opc == MOpcode::FCmpRR)
                {
                    return {true, false};
                }
                if (isCmpRI(ins.opc))
                {
                    if (idx == 0)
                        return {true, false};
                }
                if (isCset(ins.opc))
                {
                    return {false, idx == 0};
                }
                if (isMemLd(ins.opc))
                {
                    if (idx == 0)
                        return {false, true};
                }
                if (isMemSt(ins.opc))
                {
                    if (idx == 0)
                        return {true, false};
                }
                if (ins.opc == MOpcode::LdrFprFpImm)
                {
                    return {false, idx == 0};
                }
                if (ins.opc == MOpcode::StrFprFpImm || ins.opc == MOpcode::StrFprSpImm)
                {
                    if (idx == 0)
                        return {true, false};
                }
                return {true, false};
            };

            // Opportunistic spilling: if no free GPRs and we see a new def, spill the earliest state
            auto maybeSpill = [&](std::size_t operandIdx, RegClass cls) {
                if (cls == RegClass::GPR)
                {
                    if (!pools.gprFree.empty())
                        return;
                    for (auto &kv : states)
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
                    if (!pools.fprFree.empty())
                        return;
                    for (auto &kv : statesFPR)
                    {
                        if (kv.second.hasPhys)
                        {
                            spillVictim(RegClass::FPR, kv.first, prefix);
                            break;
                        }
                    }
                }
            };

            // Rewrite operands
            for (std::size_t i = 0; i < ins.ops.size(); ++i)
            {
                auto &op = ins.ops[i];
                auto [isUse, isDef] = rolesFor(i);
                if (op.kind == MOperand::Kind::Reg)
                {
                    maybeSpill(i, op.reg.cls);
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
            for (auto pr : scratch)
            {
                if (isGPR(pr))
                {
                    if (std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), pr) !=
                        ti.calleeSavedGPR.end())
                        pools.calleeUsed.insert(pr);
                    releaseGPR(pools, pr);
                }
                else
                {
                    if (std::find(ti.calleeSavedFPR.begin(), ti.calleeSavedFPR.end(), pr) !=
                        ti.calleeSavedFPR.end())
                        pools.calleeUsedFPR.insert(pr);
                    releaseFPR(pools, pr);
                }
            }
        }

        bb.instrs = std::move(rewritten);

        // Release all persistent assignments at block end
        for (auto &kv : states)
        {
            if (kv.second.hasPhys)
            {
                releaseGPR(pools, kv.second.phys);
                kv.second.hasPhys = false;
            }
        }
        for (auto &kv : statesFPR)
        {
            if (kv.second.hasPhys)
            {
                releaseFPR(pools, kv.second.phys);
                kv.second.hasPhys = false;
            }
        }
    }

    // Finalize frame size including any spills assigned
    fb.finalize();
    AllocationResult result{};

    // Callee-saved usage
    if (!pools.calleeUsed.empty())
    {
        for (auto r : ti.calleeSavedGPR)
        {
            if (pools.calleeUsed.count(r))
                fn.savedGPRs.push_back(r);
        }
    }
    if (!pools.calleeUsedFPR.empty())
    {
        for (auto r : ti.calleeSavedFPR)
        {
            if (pools.calleeUsedFPR.count(r))
                fn.savedFPRs.push_back(r);
        }
    }

    return result;
}

} // namespace viper::codegen::aarch64
