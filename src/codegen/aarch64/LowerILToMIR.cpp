//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/LowerILToMIR.cpp
// Purpose: Minimal IL→MIR lowering adapter for AArch64 (Phase A).
//
//===----------------------------------------------------------------------===//

#include "LowerILToMIR.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"

namespace viper::codegen::aarch64
{
namespace
{
using il::core::Opcode;

static const char *condForOpcode(Opcode op)
{
    switch (op)
    {
        case Opcode::ICmpEq: return "eq";
        case Opcode::ICmpNe: return "ne";
        case Opcode::SCmpLT: return "lt";
        case Opcode::SCmpLE: return "le";
        case Opcode::SCmpGT: return "gt";
        case Opcode::SCmpGE: return "ge";
        case Opcode::UCmpLT: return "lo";
        case Opcode::UCmpLE: return "ls";
        case Opcode::UCmpGT: return "hi";
        case Opcode::UCmpGE: return "hs";
        default: return nullptr;
    }
}

static int indexOfParam(const il::core::BasicBlock &bb, unsigned tempId)
{
    for (size_t i = 0; i < bb.params.size(); ++i)
        if (bb.params[i].id == tempId) return static_cast<int>(i);
    return -1;
}

} // namespace

MFunction LowerILToMIR::lowerFunction(const il::core::Function &fn) const
{
    MFunction mf{};
    mf.name = fn.name;
    // Pre-create MIR blocks with labels to mirror IL CFG shape.
    for (const auto &bb : fn.blocks)
    {
        mf.blocks.emplace_back();
        mf.blocks.back().name = bb.label;
    }

    // Helper to access a MIR block by IL block index
    auto bbOut = [&](std::size_t idx) -> MBasicBlock & { return mf.blocks[idx]; };

    if (fn.retType.kind != il::core::Type::Kind::I64)
        return mf; // Focus on i64 patterns for now

    const auto &argOrder = ti_->intArgOrder;

    if (!fn.blocks.empty())
    {
        const auto &bb = fn.blocks.front();
        auto &bbMir = bbOut(0);

        // ret %paramN fast-path
        if (fn.blocks.size() == 1 && !bb.instructions.empty() && !bb.params.empty())
        {
            const auto &retI = bb.instructions.back();
            if (retI.op == il::core::Opcode::Ret && !retI.operands.empty())
            {
                const auto &rv = retI.operands[0];
                if (rv.kind == il::core::Value::Kind::Temp)
                {
                    int pIdx = indexOfParam(bb, rv.id);
                    if (pIdx >= 0)
                    {
                        const PhysReg src = argOrder[static_cast<size_t>(pIdx)];
                        if (src != PhysReg::X0)
                            bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        return mf;
                    }
                }
            }
        }

        // rr ops on entry params feeding ret
        if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
        {
            const auto &opI = bb.instructions[bb.instructions.size() - 2];
            const auto &retI = bb.instructions.back();
            if ((opI.op == il::core::Opcode::Add || opI.op == il::core::Opcode::IAddOvf ||
                 opI.op == il::core::Opcode::Sub || opI.op == il::core::Opcode::ISubOvf ||
                 opI.op == il::core::Opcode::Mul || opI.op == il::core::Opcode::IMulOvf ||
                 opI.op == il::core::Opcode::And || opI.op == il::core::Opcode::Or ||
                 opI.op == il::core::Opcode::Xor ||
                 opI.op == il::core::Opcode::ICmpEq || opI.op == il::core::Opcode::ICmpNe ||
                 opI.op == il::core::Opcode::SCmpLT || opI.op == il::core::Opcode::SCmpLE ||
                 opI.op == il::core::Opcode::SCmpGT || opI.op == il::core::Opcode::SCmpGE ||
                 opI.op == il::core::Opcode::UCmpLT || opI.op == il::core::Opcode::UCmpLE ||
                 opI.op == il::core::Opcode::UCmpGT || opI.op == il::core::Opcode::UCmpGE) &&
                retI.op == il::core::Opcode::Ret && opI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                    opI.operands.size() == 2 && opI.operands[0].kind == il::core::Value::Kind::Temp &&
                    opI.operands[1].kind == il::core::Value::Kind::Temp)
                {
                    const int idx0 = indexOfParam(bb, opI.operands[0].id);
                    const int idx1 = indexOfParam(bb, opI.operands[1].id);
                    if (idx0 >= 0 && idx1 >= 0 && idx0 < 8 && idx1 < 8)
                    {
                        const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                        const PhysReg src1 = argOrder[static_cast<size_t>(idx1)];
                        // Normalize to x0,x1 using x9 scratch
                        bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X9), MOperand::regOp(src1)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src0)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X1), MOperand::regOp(PhysReg::X9)}});
                        switch (opI.op)
                        {
                            case Opcode::Add:
                            case Opcode::IAddOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::AddRRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Sub:
                            case Opcode::ISubOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::SubRRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Mul:
                            case Opcode::IMulOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::MulRRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::And:
                                bbMir.instrs.push_back(MInstr{MOpcode::AndRRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Or:
                                bbMir.instrs.push_back(MInstr{MOpcode::OrrRRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Xor:
                                bbMir.instrs.push_back(MInstr{MOpcode::EorRRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::ICmpEq: case Opcode::ICmpNe:
                            case Opcode::SCmpLT: case Opcode::SCmpLE: case Opcode::SCmpGT: case Opcode::SCmpGE:
                            case Opcode::UCmpLT: case Opcode::UCmpLE: case Opcode::UCmpGT: case Opcode::UCmpGE:
                                bbMir.instrs.push_back(MInstr{MOpcode::CmpRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                bbMir.instrs.push_back(MInstr{MOpcode::Cset, {MOperand::regOp(PhysReg::X0), MOperand::condOp(condForOpcode(opI.op))}});
                                break;
                            default: break;
                        }
                        return mf;
                    }
                }
            }
        }

        // ri/shift-imm and immediate compares
        if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && !bb.params.empty())
        {
            const auto &binI = bb.instructions[bb.instructions.size() - 2];
            const auto &retI = bb.instructions.back();
            // call @callee(args...) feeding ret: marshal params/consts (and one temp) into x0..x7, then bl
            if (binI.op == il::core::Opcode::Call && retI.op == il::core::Opcode::Ret &&
                binI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result && !binI.callee.empty())
                {
                    // Single-block, marshal only entry params and const i64 to integer arg regs.
                    const std::size_t nargs = binI.operands.size();
                    if (nargs <= ti_->intArgOrder.size())
                    {
                        // Build move plan for reg->reg moves; immediates applied after.
                        struct Move { PhysReg dst; PhysReg src; };
                        std::vector<Move> moves;
                        std::vector<std::pair<PhysReg,long long>> immLoads;
                        std::vector<std::pair<std::size_t,PhysReg>> tempRegs; // (arg index, reg holding computed temp)
                        const PhysReg scratchPool[] = {PhysReg::X9, PhysReg::X10};
                        std::size_t scratchUsed = 0;
                        auto isParamTemp = [&](const il::core::Value &v, unsigned &outIdx) -> bool {
                            if (v.kind != il::core::Value::Kind::Temp) return false;
                            int p = indexOfParam(bb, v.id);
                            if (p >= 0) { outIdx = static_cast<unsigned>(p); return true; }
                            return false;
                        };
                        auto computeTempTo = [&](const il::core::Instr &prod, PhysReg dstReg) -> bool {
                            // Helpers
                            auto rr_emit = [&](MOpcode opc, unsigned p0, unsigned p1) {
                                const PhysReg r0 = argOrder[p0];
                                const PhysReg r1 = argOrder[p1];
                                bbMir.instrs.push_back(MInstr{opc, {MOperand::regOp(dstReg), MOperand::regOp(r0), MOperand::regOp(r1)}});
                            };
                            auto ri_emit = [&](MOpcode opc, unsigned p0, long long imm) {
                                const PhysReg r0 = argOrder[p0];
                                bbMir.instrs.push_back(MInstr{opc, {MOperand::regOp(dstReg), MOperand::regOp(r0), MOperand::immOp(imm)}});
                            };
                            // RR patterns: both operands are entry params
                            if (prod.op == il::core::Opcode::Add || prod.op == il::core::Opcode::IAddOvf ||
                                prod.op == il::core::Opcode::Sub || prod.op == il::core::Opcode::ISubOvf ||
                                prod.op == il::core::Opcode::Mul || prod.op == il::core::Opcode::IMulOvf ||
                                prod.op == il::core::Opcode::And || prod.op == il::core::Opcode::Or ||
                                prod.op == il::core::Opcode::Xor)
                            {
                                if (prod.operands.size() != 2) return false;
                                if (prod.operands[0].kind == il::core::Value::Kind::Temp && prod.operands[1].kind == il::core::Value::Kind::Temp)
                                {
                                    int i0 = indexOfParam(bb, prod.operands[0].id);
                                    int i1 = indexOfParam(bb, prod.operands[1].id);
                                    if (i0 >= 0 && i1 >= 0 && i0 < 8 && i1 < 8)
                                    {
                                        MOpcode opc = MOpcode::AddRRR;
                                        if (prod.op == il::core::Opcode::Add || prod.op == il::core::Opcode::IAddOvf) opc = MOpcode::AddRRR;
                                        else if (prod.op == il::core::Opcode::Sub || prod.op == il::core::Opcode::ISubOvf) opc = MOpcode::SubRRR;
                                        else if (prod.op == il::core::Opcode::Mul || prod.op == il::core::Opcode::IMulOvf) opc = MOpcode::MulRRR;
                                        else if (prod.op == il::core::Opcode::And) opc = MOpcode::AndRRR;
                                        else if (prod.op == il::core::Opcode::Or) opc = MOpcode::OrrRRR;
                                        else if (prod.op == il::core::Opcode::Xor) opc = MOpcode::EorRRR;
                                        rr_emit(opc, static_cast<unsigned>(i0), static_cast<unsigned>(i1));
                                        return true;
                                    }
                                }
                            }
                            // RI patterns: param + imm for add/sub/shift
                            if (prod.op == il::core::Opcode::Shl || prod.op == il::core::Opcode::LShr || prod.op == il::core::Opcode::AShr ||
                                prod.op == il::core::Opcode::Add || prod.op == il::core::Opcode::IAddOvf ||
                                prod.op == il::core::Opcode::Sub || prod.op == il::core::Opcode::ISubOvf)
                            {
                                if (prod.operands.size() != 2) return false;
                                const auto &o0 = prod.operands[0];
                                const auto &o1 = prod.operands[1];
                                if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int ip = indexOfParam(bb, o0.id);
                                    if (ip >= 0 && ip < 8)
                                    {
                                        if (prod.op == il::core::Opcode::Shl) ri_emit(MOpcode::LslRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::LShr) ri_emit(MOpcode::LsrRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::AShr) ri_emit(MOpcode::AsrRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::Add || prod.op == il::core::Opcode::IAddOvf) ri_emit(MOpcode::AddRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::Sub || prod.op == il::core::Opcode::ISubOvf) ri_emit(MOpcode::SubRI, static_cast<unsigned>(ip), o1.i64);
                                        return true;
                                    }
                                }
                                else if (o1.kind == il::core::Value::Kind::Temp && o0.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int ip = indexOfParam(bb, o1.id);
                                    if (ip >= 0 && ip < 8)
                                    {
                                        if (prod.op == il::core::Opcode::Shl) ri_emit(MOpcode::LslRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::LShr) ri_emit(MOpcode::LsrRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::AShr) ri_emit(MOpcode::AsrRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::Add || prod.op == il::core::Opcode::IAddOvf) ri_emit(MOpcode::AddRI, static_cast<unsigned>(ip), o0.i64);
                                        // Sub with const first not supported
                                        return true;
                                    }
                                }
                            }
                            // Compare patterns: produce 0/1 in dstReg via cmp + cset
                            if (prod.op == il::core::Opcode::ICmpEq || prod.op == il::core::Opcode::ICmpNe ||
                                prod.op == il::core::Opcode::SCmpLT || prod.op == il::core::Opcode::SCmpLE ||
                                prod.op == il::core::Opcode::SCmpGT || prod.op == il::core::Opcode::SCmpGE ||
                                prod.op == il::core::Opcode::UCmpLT || prod.op == il::core::Opcode::UCmpLE ||
                                prod.op == il::core::Opcode::UCmpGT || prod.op == il::core::Opcode::UCmpGE)
                            {
                                if (prod.operands.size() != 2) return false;
                                const auto &o0 = prod.operands[0];
                                const auto &o1 = prod.operands[1];
                                const char *cc = condForOpcode(prod.op);
                                if (!cc) return false;
                                if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::Temp)
                                {
                                    int i0 = indexOfParam(bb, o0.id);
                                    int i1 = indexOfParam(bb, o1.id);
                                    if (i0 >= 0 && i1 >= 0 && i0 < 8 && i1 < 8)
                                    {
                                        const PhysReg r0 = argOrder[i0];
                                        const PhysReg r1 = argOrder[i1];
                                        bbMir.instrs.push_back(MInstr{MOpcode::CmpRR, {MOperand::regOp(r0), MOperand::regOp(r1)}});
                                        bbMir.instrs.push_back(MInstr{MOpcode::Cset, {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
                                        return true;
                                    }
                                }
                                if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int i0 = indexOfParam(bb, o0.id);
                                    if (i0 >= 0 && i0 < 8)
                                    {
                                        const PhysReg r0 = argOrder[i0];
                                        // cmp r0, #imm; cset dst, cc
                                        bbMir.instrs.push_back(MInstr{MOpcode::CmpRI, {MOperand::regOp(r0), MOperand::immOp(o1.i64)}});
                                        bbMir.instrs.push_back(MInstr{MOpcode::Cset, {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
                                        return true;
                                    }
                                }
                            }
                            return false;
                        };
                        // Split into register and stack arguments
                        const std::size_t nReg = argOrder.size();
                        const std::size_t nRegArgs = (nargs < nReg) ? nargs : nReg;
                        const std::size_t nStackArgs = (nargs > nReg) ? (nargs - nReg) : 0;
                        bool supported = true;
                        // Register args: plan moves/imm loads/temps for 0..nRegArgs-1
                        for (std::size_t i = 0; i < nRegArgs; ++i)
                        {
                            const PhysReg dst = argOrder[i];
                            const auto &arg = binI.operands[i];
                            if (arg.kind == il::core::Value::Kind::ConstInt)
                            {
                                immLoads.emplace_back(dst, arg.i64);
                            }
                            else
                            {
                                unsigned pIdx = 0;
                                if (isParamTemp(arg, pIdx) && pIdx < argOrder.size())
                                {
                                    const PhysReg src = argOrder[pIdx];
                                    if (src != dst)
                                        moves.push_back(Move{dst, src});
                                }
                                else
                                {
                                    // Attempt to compute temp into a scratch then marshal it.
                                    if (arg.kind == il::core::Value::Kind::Temp && scratchUsed < (sizeof(scratchPool)/sizeof(scratchPool[0])))
                                    {
                                        auto it = std::find_if(bb.instructions.begin(), bb.instructions.end(),
                                                               [&](const il::core::Instr &I){ return I.result && *I.result == arg.id; });
                                        if (it != bb.instructions.end())
                                        {
                                            const PhysReg dstScratch = scratchPool[scratchUsed];
                                            if (computeTempTo(*it, dstScratch))
                                            {
                                                tempRegs.emplace_back(i, dstScratch);
                                                ++scratchUsed;
                                                continue;
                                            }
                                        }
                                    }
                                    supported = false; break; // unsupported temp
                                }
                            }
                        }
                        if (!supported) { /* fallthrough: no call lowering */ }
                        else
                        {
                            // Include temp-reg moves into overall move list
                            for (auto &tr : tempRegs)
                            {
                                const PhysReg dstArg = argOrder[tr.first];
                                if (dstArg != tr.second)
                                    moves.push_back(Move{dstArg, tr.second});
                            }
                            // Resolve reg moves with scratch X9 to break cycles.
                            auto hasDst = [&](PhysReg r){ for (auto &m : moves) if (m.dst==r) return true; return false; };
                            // Perform until empty
                            while (!moves.empty())
                            {
                                bool progressed = false;
                                for (auto it = moves.begin(); it != moves.end(); )
                                {
                                    if (!hasDst(it->src))
                                    {
                                        bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(it->dst), MOperand::regOp(it->src)}});
                                        it = moves.erase(it);
                                        progressed = true;
                                    }
                                    else { ++it; }
                                }
                                if (!progressed)
                                {
                                    const PhysReg cycleSrc = moves.front().src;
                                    bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X9), MOperand::regOp(cycleSrc)}});
                                    for (auto &m : moves)
                                        if (m.src == cycleSrc) m.src = PhysReg::X9;
                                }
                            }
                            // Apply immediates
                            for (auto &pr : immLoads)
                                bbMir.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(pr.first), MOperand::immOp(pr.second)}});

                            // Stack args: allocate area, materialize values, store at [sp, #offset]
                            if (nStackArgs > 0)
                            {
                                long long frameBytes = static_cast<long long>(nStackArgs) * 8LL;
                                if (frameBytes % 16LL != 0LL) frameBytes += 8LL; // 16-byte alignment
                                bbMir.instrs.push_back(MInstr{MOpcode::SubSpImm, {MOperand::immOp(frameBytes)}});
                                for (std::size_t i = nReg; i < nargs; ++i)
                                {
                                    const auto &arg = binI.operands[i];
                                    PhysReg valReg = PhysReg::X9;
                                    if (arg.kind == il::core::Value::Kind::ConstInt)
                                    {
                                        // Use a scratch reg to hold the constant
                                        const PhysReg tmp = (scratchUsed < (sizeof(scratchPool)/sizeof(scratchPool[0]))) ? scratchPool[scratchUsed++] : PhysReg::X9;
                                        bbMir.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(tmp), MOperand::immOp(arg.i64)}});
                                        valReg = tmp;
                                    }
                                    else if (arg.kind == il::core::Value::Kind::Temp)
                                    {
                                        unsigned pIdx = 0;
                                        if (isParamTemp(arg, pIdx) && pIdx < argOrder.size())
                                        {
                                            valReg = argOrder[pIdx];
                                        }
                                        else
                                        {
                                            // Compute limited temps into scratch
                                            if (scratchUsed >= (sizeof(scratchPool)/sizeof(scratchPool[0]))) { supported = false; break; }
                                            valReg = scratchPool[scratchUsed++];
                                            auto it = std::find_if(bb.instructions.begin(), bb.instructions.end(),
                                                                   [&](const il::core::Instr &I){ return I.result && *I.result == arg.id; });
                                            if (it == bb.instructions.end() || !computeTempTo(*it, valReg)) { supported = false; break; }
                                        }
                                    }
                                    else { supported = false; break; }
                                    const long long off = static_cast<long long>((i - nReg) * 8ULL);
                                    bbMir.instrs.push_back(MInstr{MOpcode::StrRegSpImm, {MOperand::regOp(valReg), MOperand::immOp(off)}});
                                }
                                if (!supported)
                                {
                                    // If unsupported mid-way, do not emit call sequence.
                                    return mf;
                                }
                                // Emit call and deallocate area
                                bbMir.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
                                bbMir.instrs.push_back(MInstr{MOpcode::AddSpImm, {MOperand::immOp(frameBytes)}});
                                return mf;
                            }
                            // No stack args; emit call directly
                            bbMir.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
                            return mf;
                        }
                    }
                }
            }
            const bool isAdd = (binI.op == il::core::Opcode::Add || binI.op == il::core::Opcode::IAddOvf);
            const bool isSub = (binI.op == il::core::Opcode::Sub || binI.op == il::core::Opcode::ISubOvf);
            const bool isShl = (binI.op == il::core::Opcode::Shl);
            const bool isLShr = (binI.op == il::core::Opcode::LShr);
            const bool isAShr = (binI.op == il::core::Opcode::AShr);
            const bool isICmpImm = (condForOpcode(binI.op) != nullptr);
            if ((isAdd || isSub || isShl || isLShr || isAShr) && retI.op == il::core::Opcode::Ret && binI.result &&
                !retI.operands.empty() && binI.operands.size() == 2)
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                {
                    const auto &o0 = binI.operands[0];
                    const auto &o1 = binI.operands[1];
                    auto emitImm = [&](unsigned paramIndex, long long imm)
                    {
                        const PhysReg src = argOrder[paramIndex];
                        bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        if (isAdd) bbMir.instrs.push_back(MInstr{MOpcode::AddRI, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        else if (isSub) bbMir.instrs.push_back(MInstr{MOpcode::SubRI, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        else if (isShl) bbMir.instrs.push_back(MInstr{MOpcode::LslRI, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        else if (isLShr) bbMir.instrs.push_back(MInstr{MOpcode::LsrRI, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        else if (isAShr) bbMir.instrs.push_back(MInstr{MOpcode::AsrRI, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                    };
                    if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                    {
                        for (size_t i = 0; i < bb.params.size(); ++i)
                            if (bb.params[i].id == o0.id && i < 8) { emitImm(static_cast<unsigned>(i), o1.i64); return mf; }
                    }
                    if (o1.kind == il::core::Value::Kind::Temp && o0.kind == il::core::Value::Kind::ConstInt)
                    {
                        if (isAdd || isShl || isLShr || isAShr)
                        {
                            for (size_t i = 0; i < bb.params.size(); ++i)
                                if (bb.params[i].id == o1.id && i < 8) { emitImm(static_cast<unsigned>(i), o0.i64); return mf; }
                        }
                    }
                }
            }
            if (isICmpImm && retI.op == il::core::Opcode::Ret && binI.result && !retI.operands.empty() &&
                binI.operands.size() == 2)
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                {
                    const auto &o0 = binI.operands[0];
                    const auto &o1 = binI.operands[1];
                    auto emitCmpImm = [&](unsigned paramIndex, long long imm)
                    {
                        const PhysReg src = argOrder[paramIndex];
                        if (src != PhysReg::X0)
                            bbMir.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::CmpRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::Cset, {MOperand::regOp(PhysReg::X0), MOperand::condOp(condForOpcode(binI.op))}});
                    };
                    if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                    {
                        for (size_t i = 0; i < bb.params.size(); ++i)
                            if (bb.params[i].id == o0.id && i < 8) { emitCmpImm(static_cast<unsigned>(i), o1.i64); return mf; }
                    }
                }
            }
        }

        // ret const i64 short-path: only when function is single-block.
        if (fn.blocks.size() == 1)
        {
            const auto &only = fn.blocks.front();
            if (!only.instructions.empty())
            {
                const auto &term = only.instructions.back();
                if (term.op == il::core::Opcode::Ret && !term.operands.empty())
                {
                    const auto &v = term.operands[0];
                    if (v.kind == il::core::Value::Kind::ConstInt)
                    {
                        const long long imm = v.i64;
                        // Prefer movz/movk path in AsmEmitter for wide values.
                        bbOut(0).instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        return mf;
                    }
                }
            }
        }

        // Lower simple control-flow terminators: br and cbr
        for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        {
            const auto &inBB = fn.blocks[i];
            if (inBB.instructions.empty()) continue;
            const auto &term = inBB.instructions.back();
            auto &outBB = bbOut(i);
            switch (term.op)
            {
                case il::core::Opcode::Br:
                    if (!term.labels.empty())
                        outBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[0])}});
                    break;
                case il::core::Opcode::CBr:
                    if (term.operands.size() >= 1 && term.labels.size() == 2)
                    {
                        const auto &cond = term.operands[0];
                        // If constant, fold to unconditional branch.
                        if (cond.kind == il::core::Value::Kind::ConstInt)
                        {
                            const auto &lbl = (cond.i64 != 0) ? term.labels[0] : term.labels[1];
                            outBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(lbl)}});
                        }
                        else
                        {
                            bool loweredViaCompare = false;
                            // If cond is produced by a compare, emit cmp + b.<cond>
                            if (cond.kind == il::core::Value::Kind::Temp)
                            {
                                const auto it = std::find_if(
                                    inBB.instructions.begin(), inBB.instructions.end(),
                                    [&](const il::core::Instr &I) { return I.result && *I.result == cond.id; });
                                if (it != inBB.instructions.end())
                                {
                                    const il::core::Instr &cmpI = *it;
                                    const char *cc = condForOpcode(cmpI.op);
                                    if (cc && cmpI.operands.size() == 2)
                                    {
                                        const auto &o0 = cmpI.operands[0];
                                        const auto &o1 = cmpI.operands[1];
                                        if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::Temp)
                                        {
                                            int idx0 = indexOfParam(inBB, o0.id);
                                            int idx1 = indexOfParam(inBB, o1.id);
                                            if (idx0 >= 0 && idx1 >= 0 && idx0 < 8 && idx1 < 8)
                                            {
                                                const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                                                const PhysReg src1 = argOrder[static_cast<size_t>(idx1)];
                                                outBB.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X9), MOperand::regOp(src1)}});
                                                outBB.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src0)}});
                                                outBB.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X1), MOperand::regOp(PhysReg::X9)}});
                                                outBB.instrs.push_back(MInstr{MOpcode::CmpRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                                outBB.instrs.push_back(MInstr{MOpcode::BCond, {MOperand::condOp(cc), MOperand::labelOp(term.labels[0])}});
                                                outBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[1])}});
                                                loweredViaCompare = true;
                                            }
                                        }
                                        else if (o0.kind == il::core::Value::Kind::Temp && o1.kind == il::core::Value::Kind::ConstInt)
                                        {
                                            int idx0 = indexOfParam(inBB, o0.id);
                                            if (idx0 >= 0 && idx0 < 8)
                                            {
                                                const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                                                if (src0 != PhysReg::X0)
                                                    outBB.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src0)}});
                                                outBB.instrs.push_back(MInstr{MOpcode::CmpRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(o1.i64)}});
                                                outBB.instrs.push_back(MInstr{MOpcode::BCond, {MOperand::condOp(cc), MOperand::labelOp(term.labels[0])}});
                                                outBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[1])}});
                                                loweredViaCompare = true;
                                            }
                                        }
                                    }
                                }
                            }
                            if (!loweredViaCompare)
                            {
                                // Fallback: assume cond in x0 (or move param to x0), then cmp x0, #0; b.ne true; b false
                                if (cond.kind == il::core::Value::Kind::Temp)
                                {
                                    int pIdx = indexOfParam(inBB, cond.id);
                                    if (pIdx >= 0)
                                    {
                                        const PhysReg src = argOrder[static_cast<size_t>(pIdx)];
                                        if (src != PhysReg::X0)
                                            outBB.instrs.push_back(MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                                    }
                                }
                                outBB.instrs.push_back(MInstr{MOpcode::CmpRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
                                outBB.instrs.push_back(MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(term.labels[0])}});
                                outBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[1])}});
                            }
                        }
                    }
                    break;
                default: break;
            }
        }
    }
    return mf;
}

} // namespace viper::codegen::aarch64
