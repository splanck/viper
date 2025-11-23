//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "FrameBuilder.hpp"
#include "OpcodeMappings.hpp"

#include <optional>
#include <unordered_map>

namespace viper::codegen::aarch64
{
namespace
{
using il::core::Opcode;

static const char *condForOpcode(Opcode op)
{
    return lookupCondition(op);
}

static int indexOfParam(const il::core::BasicBlock &bb, unsigned tempId)
{
    for (size_t i = 0; i < bb.params.size(); ++i)
        if (bb.params[i].id == tempId)
            return static_cast<int>(i);
    return -1;
}

// Helper describing a lowered call sequence
struct LoweredCall
{
    std::vector<MInstr> prefix; // arg materialization and marshalling
    MInstr call;                // Bl callee
    std::vector<MInstr> postfix; // any clean-up (currently empty)
};

// Materialize an IL value into a vreg and append MIR into out. Returns the vreg id.
static bool materializeValueToVReg(const il::core::Value &v,
                                   const il::core::BasicBlock &bb,
                                   const TargetInfo &ti,
                                   FrameBuilder &fb,
                                   MBasicBlock &out,
                                   std::unordered_map<unsigned, uint16_t> &tempVReg,
                                   uint16_t &nextVRegId,
                                   uint16_t &outVReg,
                                   RegClass &outCls)
{
    if (v.kind == il::core::Value::Kind::ConstInt)
    {
        outVReg = nextVRegId++;
        outCls = RegClass::GPR;
        out.instrs.push_back(MInstr{MOpcode::MovRI,
                                    {MOperand::vregOp(outCls, outVReg), MOperand::immOp(v.i64)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::ConstFloat)
    {
        outVReg = nextVRegId++;
        outCls = RegClass::FPR;
        long long bits;
        static_assert(sizeof(double) == sizeof(long long), "size");
        std::memcpy(&bits, &v.f64, sizeof(double));
        out.instrs.push_back(MInstr{MOpcode::FMovRI,
                                    {MOperand::vregOp(outCls, outVReg), MOperand::immOp(bits)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::Temp)
    {
        // If it's an entry param, move from ABI phys -> vreg
        int pIdx = indexOfParam(bb, v.id);
        if (pIdx >= 0 && pIdx < static_cast<int>(ti.intArgOrder.size()))
        {
            // Determine param type
            RegClass cls = RegClass::GPR;
            if (pIdx < static_cast<int>(bb.params.size()) &&
                bb.params[static_cast<std::size_t>(pIdx)].type.kind == il::core::Type::Kind::F64)
            {
                cls = RegClass::FPR;
            }
            outVReg = nextVRegId++;
            outCls = cls;
            if (cls == RegClass::GPR)
            {
                const PhysReg src = ti.intArgOrder[static_cast<std::size_t>(pIdx)];
                out.instrs.push_back(MInstr{MOpcode::MovRR,
                                            {MOperand::vregOp(cls, outVReg), MOperand::regOp(src)}});
            }
            else
            {
                const PhysReg src = ti.f64ArgOrder[static_cast<std::size_t>(pIdx)];
                out.instrs.push_back(MInstr{MOpcode::FMovRR,
                                            {MOperand::vregOp(cls, outVReg), MOperand::regOp(src)}});
            }
            return true;
        }
        // If we already materialized this temp earlier, reuse
        auto it = tempVReg.find(v.id);
        if (it != tempVReg.end())
        {
            outVReg = it->second;
            // Assume GPR class by default; complex tracking omitted
            outCls = RegClass::GPR;
            return true;
        }
        // Find the producing instruction within the block and lower a subset
        auto prodIt = std::find_if(bb.instructions.begin(), bb.instructions.end(), [&](const il::core::Instr &I) {
            return I.result && *I.result == v.id; });
        if (prodIt == bb.instructions.end())
            return false;

        auto emitRRR = [&](MOpcode opc, const il::core::Value &a, const il::core::Value &b) -> bool {
            uint16_t va = 0, vb = 0; RegClass ca=RegClass::GPR, cb=RegClass::GPR;
            if (!materializeValueToVReg(a, bb, ti, fb, out, tempVReg, nextVRegId, va, ca))
                return false;
            if (!materializeValueToVReg(b, bb, ti, fb, out, tempVReg, nextVRegId, vb, cb))
                return false;
            outVReg = nextVRegId++;
            outCls = (opc == MOpcode::FAddRRR || opc == MOpcode::FSubRRR || opc == MOpcode::FMulRRR || opc == MOpcode::FDivRRR) ? RegClass::FPR : RegClass::GPR;
            out.instrs.push_back(MInstr{opc,
                                        {MOperand::vregOp(outCls, outVReg),
                                         MOperand::vregOp(outCls, va),
                                         MOperand::vregOp(outCls, vb)}});
            return true;
        };
        auto emitRImm = [&](MOpcode opc, const il::core::Value &a, long long imm) -> bool {
            uint16_t va = 0; RegClass ca=RegClass::GPR;
            if (!materializeValueToVReg(a, bb, ti, fb, out, tempVReg, nextVRegId, va, ca))
                return false;
            outVReg = nextVRegId++;
            outCls = (opc == MOpcode::FAddRRR || opc == MOpcode::FSubRRR || opc == MOpcode::FMulRRR || opc == MOpcode::FDivRRR) ? RegClass::FPR : RegClass::GPR;
            out.instrs.push_back(MInstr{opc,
                                        {MOperand::vregOp(outCls, outVReg),
                                         MOperand::vregOp(outCls, va),
                                         MOperand::immOp(imm)}});
            return true;
        };

        const auto &prod = *prodIt;

        // Check for binary operations first using table lookup
        if (const auto* binOp = lookupBinaryOp(prod.op)) {
            if (prod.operands.size() == 2) {
                // Check if this is a shift operation that requires immediate
                bool isShift = (prod.op == Opcode::Shl || prod.op == Opcode::LShr || prod.op == Opcode::AShr);

                if (binOp->supportsImmediate && prod.operands[1].kind == il::core::Value::Kind::ConstInt) {
                    return emitRImm(binOp->immOp, prod.operands[0], prod.operands[1].i64);
                } else if (!isShift) {
                    // Non-shift operations can use register-register form
                    return emitRRR(binOp->mirOp, prod.operands[0], prod.operands[1]);
                }
            }
        }

        // Handle other operations
        switch (prod.op)
        {
            case Opcode::ConstStr:
                if (!prod.operands.empty() && prod.operands[0].kind == il::core::Value::Kind::GlobalAddr)
                {
                    outVReg = nextVRegId++;
                    outCls = RegClass::GPR;
                    const std::string &sym = prod.operands[0].str;
                    out.instrs.push_back(MInstr{MOpcode::AdrPage,
                                                {MOperand::vregOp(RegClass::GPR, outVReg),
                                                 MOperand::labelOp(sym)}});
                    out.instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                {MOperand::vregOp(RegClass::GPR, outVReg),
                                                 MOperand::vregOp(RegClass::GPR, outVReg),
                                                 MOperand::labelOp(sym)}});
                    return true;
                }
                break;
            default:
                // Check if it's a comparison operation
                if (isCompareOp(prod.op)) {
                    if (prod.operands.size() == 2)
                    {
                        uint16_t va = 0, vb = 0;
                        RegClass ca=RegClass::GPR, cb=RegClass::GPR;
                        if (!materializeValueToVReg(prod.operands[0], bb, ti, fb, out, tempVReg, nextVRegId, va, ca))
                            return false;
                        if (!materializeValueToVReg(prod.operands[1], bb, ti, fb, out, tempVReg, nextVRegId, vb, cb))
                            return false;
                        out.instrs.push_back(MInstr{MOpcode::CmpRR,
                                                    {MOperand::vregOp(RegClass::GPR, va),
                                                     MOperand::vregOp(RegClass::GPR, vb)}});
                        outVReg = nextVRegId++;
                        outCls = RegClass::GPR;
                        out.instrs.push_back(MInstr{MOpcode::Cset,
                                                    {MOperand::vregOp(RegClass::GPR, outVReg),
                                                     MOperand::condOp(condForOpcode(prod.op))}});
                        return true;
                    }
                }
                break;
            case Opcode::Load:
                if (!prod.operands.empty() && prod.operands[0].kind == il::core::Value::Kind::Temp)
                {
                    const unsigned allocaId = prod.operands[0].id;
                    const int off = fb.localOffset(allocaId);
                    if (off != 0)
                    {
                        outVReg = nextVRegId++;
                        outCls = RegClass::GPR;
                        out.instrs.push_back(MInstr{MOpcode::LdrRegFpImm,
                                                    {MOperand::vregOp(outCls, outVReg),
                                                     MOperand::immOp(off)}});
                        return true;
                    }
                }
                break;
        }
    }
    return false;
}

static bool lowerCallWithArgs(const il::core::Instr &callI,
                              const il::core::BasicBlock &bb,
                              const TargetInfo &ti,
                              FrameBuilder &fb,
                              MBasicBlock &out,
                              LoweredCall &seq)
{
    if (callI.op != il::core::Opcode::Call)
        return false;
    const std::size_t nargs = callI.operands.size();
    // Track max outgoing bytes for frame reservation
    std::size_t nStack = (nargs > ti.intArgOrder.size()) ? (nargs - ti.intArgOrder.size()) : 0;
    if (nStack > 0)
    {
        int bytes = static_cast<int>(nStack) * kSlotSizeBytes;
        if (bytes % kStackAlignment != 0)
            bytes = (bytes + (kStackAlignment - 1)) & ~(kStackAlignment - 1);
        fb.setMaxOutgoingBytes(bytes);
    }

    // Materialize each argument into a vreg with class
    std::unordered_map<unsigned, uint16_t> tempVReg; // IL temp -> vreg id
    uint16_t nextVRegId = 1;                         // vreg ids start at 1
    std::vector<uint16_t> argvregs;
    std::vector<RegClass> argvcls;
    argvregs.reserve(nargs);
    for (std::size_t i = 0; i < nargs; ++i)
    {
        uint16_t vreg = 0; RegClass cls = RegClass::GPR;
        if (!materializeValueToVReg(callI.operands[i], bb, ti, fb, out, tempVReg, nextVRegId, vreg, cls))
            return false;
        argvregs.push_back(vreg);
        argvcls.push_back(cls);
    }
    // Moves to ABI registers by class
    std::size_t usedG = 0, usedF = 0;
    for (std::size_t i = 0; i < nargs; ++i)
    {
        if (argvcls[i] == RegClass::GPR)
        {
            if (usedG < ti.intArgOrder.size())
            {
                seq.prefix.push_back(MInstr{MOpcode::MovRR,
                                            {MOperand::regOp(ti.intArgOrder[usedG++]),
                                             MOperand::vregOp(RegClass::GPR, argvregs[i])}});
            }
        }
        else
        {
            if (usedF < ti.f64ArgOrder.size())
            {
                seq.prefix.push_back(MInstr{MOpcode::FMovRR,
                                            {MOperand::regOp(ti.f64ArgOrder[usedF++]),
                                             MOperand::vregOp(RegClass::FPR, argvregs[i])}});
            }
        }
    }
    // Stack args into reserved outgoing area at [sp, #off] for any overflow
    std::size_t gSpilled = (usedG < nargs) ? 0 : 0; // unused
    for (std::size_t i = 0; i < nargs; ++i)
    {
        bool needStack = (argvcls[i] == RegClass::GPR) ? (usedG >= ti.intArgOrder.size() && i >= usedG)
                                                       : (usedF >= ti.f64ArgOrder.size() && i >= usedF);
        // Simpler: place any args beyond 8 in stack in order after register args of same class
        std::size_t offIndex = 0;
        if (argvcls[i] == RegClass::GPR && usedG >= ti.intArgOrder.size())
        {
            offIndex = i - usedG;
            const long long off = static_cast<long long>(offIndex * kSlotSizeBytes);
            seq.prefix.push_back(MInstr{MOpcode::StrRegSpImm,
                                        {MOperand::vregOp(RegClass::GPR, argvregs[i]),
                                         MOperand::immOp(off)}});
        }
        if (argvcls[i] == RegClass::FPR && usedF >= ti.f64ArgOrder.size())
        {
            offIndex = i - usedF;
            const long long off = static_cast<long long>(offIndex * kSlotSizeBytes);
            seq.prefix.push_back(MInstr{MOpcode::StrFprSpImm,
                                        {MOperand::vregOp(RegClass::FPR, argvregs[i]),
                                         MOperand::immOp(off)}});
        }
    }
    seq.call = MInstr{MOpcode::Bl, {MOperand::labelOp(callI.callee)}};
    return true;
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

    // Build stack frame locals from allocas (simple i64 scalar locals only)
    FrameBuilder fb{mf};
    for (const auto &bb : fn.blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == il::core::Opcode::Alloca && instr.result && !instr.operands.empty())
            {
                if (instr.operands[0].kind == il::core::Value::Kind::ConstInt &&
                    instr.operands[0].i64 == 8)
                {
                    fb.addLocal(*instr.result, 8, 8);
                }
            }
        }
    }

    // Helper to get the register holding a value
    auto getValueReg = [&](const il::core::BasicBlock &bb,
                           const il::core::Value &val) -> std::optional<PhysReg>
    {
        if (val.kind == il::core::Value::Kind::Temp)
        {
            // Check if it's a parameter
            int pIdx = indexOfParam(bb, val.id);
            if (pIdx >= 0 && pIdx < 8)
            {
                return argOrder[static_cast<size_t>(pIdx)];
            }
        }
        return std::nullopt;
    };

    if (!fn.blocks.empty())
    {
        const auto &bb = fn.blocks.front();
        auto &bbMir = bbOut(0);

        // Simple alloca/store/load/ret pattern: %local = alloca i64; store %param0, %local;
        // %val = load %local; ret %val
        if (!mf.frame.locals.empty() && fn.blocks.size() == 1 && bb.instructions.size() >= 4)
        {
            // Look for: alloca, store, load, ret
            const auto *allocaI = &bb.instructions[bb.instructions.size() - 4];
            const auto *storeI = &bb.instructions[bb.instructions.size() - 3];
            const auto *loadI = &bb.instructions[bb.instructions.size() - 2];
            const auto *retI = &bb.instructions[bb.instructions.size() - 1];

            if (allocaI->op == il::core::Opcode::Alloca && allocaI->result &&
                storeI->op == il::core::Opcode::Store && storeI->operands.size() == 2 &&
                loadI->op == il::core::Opcode::Load && loadI->result &&
                loadI->operands.size() == 1 && retI->op == il::core::Opcode::Ret &&
                !retI->operands.empty())
            {
                const unsigned allocaId = *allocaI->result;
                const auto &storePtr = storeI->operands[0]; // pointer is operand 0
                const auto &storeVal = storeI->operands[1]; // value is operand 1
                const auto &loadPtr = loadI->operands[0];
                const auto &retVal = retI->operands[0];

                // Check that store and load both target the same alloca
                if (storePtr.kind == il::core::Value::Kind::Temp &&
                    storePtr.id == allocaId && loadPtr.kind == il::core::Value::Kind::Temp &&
                    loadPtr.id == allocaId &&
                    retVal.kind == il::core::Value::Kind::Temp && retVal.id == *loadI->result)
                {
                    // Get offset for this alloca
                    // Query assigned offset from frame builder
                    const int offset = fb.localOffset(allocaId);
                    if (offset != 0)
                    {
                        // Get register holding the value to store
                        auto srcReg = getValueReg(bb, storeVal);
                        if (srcReg)
                        {
                            // str srcReg, [x29, #offset]
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::StrRegFpImm,
                                       {MOperand::regOp(*srcReg), MOperand::immOp(offset)}});
                            // ldr x0, [x29, #offset]
                            bbMir.instrs.push_back(MInstr{
                                MOpcode::LdrRegFpImm,
                                {MOperand::regOp(PhysReg::X0), MOperand::immOp(offset)}});
                            fb.finalize();
                            return mf;
                        }
                    }
                }
            }
        }

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
                        if (fn.retType.kind == il::core::Type::Kind::F64)
                        {
                            const PhysReg src = ti_->f64ArgOrder[static_cast<size_t>(pIdx)];
                            if (src != PhysReg::V0)
                                bbMir.instrs.push_back(MInstr{MOpcode::FMovRR,
                                                              {MOperand::regOp(PhysReg::V0),
                                                               MOperand::regOp(src)}});
                        }
                        else
                        {
                            const PhysReg src = argOrder[static_cast<size_t>(pIdx)];
                            if (src != PhysReg::X0)
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::MovRR,
                                           {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        }
                        fb.finalize();
                        fb.finalize();
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
                 opI.op == il::core::Opcode::Xor || opI.op == il::core::Opcode::ICmpEq ||
                 opI.op == il::core::Opcode::ICmpNe || opI.op == il::core::Opcode::SCmpLT ||
                 opI.op == il::core::Opcode::SCmpLE || opI.op == il::core::Opcode::SCmpGT ||
                 opI.op == il::core::Opcode::SCmpGE || opI.op == il::core::Opcode::UCmpLT ||
                 opI.op == il::core::Opcode::UCmpLE || opI.op == il::core::Opcode::UCmpGT ||
                 opI.op == il::core::Opcode::UCmpGE) &&
                retI.op == il::core::Opcode::Ret && opI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                    opI.operands.size() == 2 &&
                    opI.operands[0].kind == il::core::Value::Kind::Temp &&
                    opI.operands[1].kind == il::core::Value::Kind::Temp)
                {
                    const int idx0 = indexOfParam(bb, opI.operands[0].id);
                    const int idx1 = indexOfParam(bb, opI.operands[1].id);
                    if (idx0 >= 0 && idx1 >= 0 && idx0 < 8 && idx1 < 8)
                    {
                        const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                        const PhysReg src1 = argOrder[static_cast<size_t>(idx1)];
                        // Normalize to x0,x1 using x9 scratch
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(PhysReg::X9), MOperand::regOp(src1)}});
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src0)}});
                        bbMir.instrs.push_back(
                            MInstr{MOpcode::MovRR,
                                   {MOperand::regOp(PhysReg::X1), MOperand::regOp(PhysReg::X9)}});
                        switch (opI.op)
                        {
                            case Opcode::Add:
                            case Opcode::IAddOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::AddRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Sub:
                            case Opcode::ISubOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::SubRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Mul:
                            case Opcode::IMulOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::MulRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::And:
                                bbMir.instrs.push_back(MInstr{MOpcode::AndRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Or:
                                bbMir.instrs.push_back(MInstr{MOpcode::OrrRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Xor:
                                bbMir.instrs.push_back(MInstr{MOpcode::EorRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::ICmpEq:
                            case Opcode::ICmpNe:
                            case Opcode::SCmpLT:
                            case Opcode::SCmpLE:
                            case Opcode::SCmpGT:
                            case Opcode::SCmpGE:
                            case Opcode::UCmpLT:
                            case Opcode::UCmpLE:
                            case Opcode::UCmpGT:
                            case Opcode::UCmpGE:
                                bbMir.instrs.push_back(MInstr{
                                    MOpcode::CmpRR,
                                    {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::Cset,
                                           {MOperand::regOp(PhysReg::X0),
                                            MOperand::condOp(condForOpcode(opI.op))}});
                                break;
                            default:
                                break;
                        }
                        return mf;
                    }
                }
            }
        }

        // Floating rr ops on entry params feeding ret (fadd/fsub/fmul/fdiv)
        if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
        {
            const auto &opI = bb.instructions[bb.instructions.size() - 2];
            const auto &retI = bb.instructions.back();
            const bool isFAdd = (opI.op == il::core::Opcode::FAdd);
            const bool isFSub = (opI.op == il::core::Opcode::FSub);
            const bool isFMul = (opI.op == il::core::Opcode::FMul);
            const bool isFDiv = (opI.op == il::core::Opcode::FDiv);
            if ((isFAdd || isFSub || isFMul || isFDiv) && retI.op == il::core::Opcode::Ret &&
                opI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                    opI.operands.size() == 2 &&
                    opI.operands[0].kind == il::core::Value::Kind::Temp &&
                    opI.operands[1].kind == il::core::Value::Kind::Temp)
                {
                    const int idx0 = indexOfParam(bb, opI.operands[0].id);
                    const int idx1 = indexOfParam(bb, opI.operands[1].id);
                    if (idx0 >= 0 && idx1 >= 0 && idx0 < 8 && idx1 < 8)
                    {
                        const PhysReg src0 = ti_->f64ArgOrder[static_cast<std::size_t>(idx0)];
                        const PhysReg src1 = ti_->f64ArgOrder[static_cast<std::size_t>(idx1)];
                        // Normalize to d0,d1 using v16 as scratch
                        bbMir.instrs.push_back(MInstr{MOpcode::FMovRR,
                                                      {MOperand::regOp(PhysReg::V16),
                                                       MOperand::regOp(src1)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::FMovRR,
                                                      {MOperand::regOp(PhysReg::V0),
                                                       MOperand::regOp(src0)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::FMovRR,
                                                      {MOperand::regOp(PhysReg::V1),
                                                       MOperand::regOp(PhysReg::V16)}});
                        if (isFAdd)
                            bbMir.instrs.push_back(MInstr{MOpcode::FAddRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        else if (isFSub)
                            bbMir.instrs.push_back(MInstr{MOpcode::FSubRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        else if (isFMul)
                            bbMir.instrs.push_back(MInstr{MOpcode::FMulRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        else if (isFDiv)
                            bbMir.instrs.push_back(MInstr{MOpcode::FDivRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        fb.finalize();
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
            // Unary integer/FP casts feeding return
            if (retI.op == il::core::Opcode::Ret && binI.result && !retI.operands.empty() &&
                retI.operands[0].kind == il::core::Value::Kind::Temp &&
                retI.operands[0].id == *binI.result)
            {
                // zext1: zero-extend i1 -> i64, or trunc1: i64->i1
                if (binI.op == il::core::Opcode::Zext1 || binI.op == il::core::Opcode::Trunc1)
                {
                    // Operand may be param or temp; handle param only for Phase A
                    const auto &o0 = binI.operands[0];
                    PhysReg src = PhysReg::X0;
                    if (o0.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(bb, o0.id);
                        if (pIdx >= 0)
                            src = argOrder[static_cast<std::size_t>(pIdx)];
                    }
                    if (src != PhysReg::X0)
                        bbMir.instrs.push_back(MInstr{MOpcode::MovRR,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(src)}});
                    // and x0, x0, #1 via tmp reg
                    bbMir.instrs.push_back(MInstr{MOpcode::MovRI,
                                                  {MOperand::regOp(PhysReg::X9),
                                                   MOperand::immOp(1)}});
                    bbMir.instrs.push_back(MInstr{MOpcode::AndRRR,
                                                  {MOperand::regOp(PhysReg::X0),
                                                   MOperand::regOp(PhysReg::X0),
                                                   MOperand::regOp(PhysReg::X9)}});
                    fb.finalize();
                    return mf;
                }
                // cast.si_narrow.chk: signed narrowing to target width with range check
                if (binI.op == il::core::Opcode::CastSiNarrowChk)
                {
                    // Determine target width from binI.type
                    int bits = 64;
                    if (binI.type.kind == il::core::Type::Kind::I16)
                        bits = 16;
                    else if (binI.type.kind == il::core::Type::Kind::I32)
                        bits = 32;
                    else if (binI.type.kind == il::core::Type::Kind::I64)
                        bits = 64;
                    const int sh = 64 - bits;
                    const auto &o0 = binI.operands[0];
                    PhysReg src = PhysReg::X0;
                    if (o0.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(bb, o0.id);
                        if (pIdx >= 0)
                            src = argOrder[static_cast<std::size_t>(pIdx)];
                    }
                    if (src != PhysReg::X0)
                        bbMir.instrs.push_back(MInstr{MOpcode::MovRR,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(src)}});
                    // tmp = (x0 << sh) >> sh  (sign-extended truncation)
                    if (sh > 0)
                    {
                        bbMir.instrs.push_back(MInstr{MOpcode::LslRI,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(PhysReg::X0),
                                                       MOperand::immOp(sh)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::AsrRI,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(PhysReg::X0),
                                                       MOperand::immOp(sh)}});
                    }
                    // Compare restored value to source in X9
                    bbMir.instrs.push_back(MInstr{MOpcode::MovRR,
                                                  {MOperand::regOp(PhysReg::X9),
                                                   MOperand::regOp(src)}});
                    bbMir.instrs.push_back(MInstr{MOpcode::CmpRR,
                                                  {MOperand::regOp(PhysReg::X0),
                                                   MOperand::regOp(PhysReg::X9)}});
                    // If not equal, branch to a trap block
                    const std::string trapLabel = ".Ltrap_cast";
                    bbMir.instrs.push_back(MInstr{MOpcode::BCond,
                                                  {MOperand::condOp("ne"),
                                                   MOperand::labelOp(trapLabel)}});
                    // Append trap block to function with a call to rt_trap
                    mf.blocks.emplace_back();
                    mf.blocks.back().name = trapLabel;
                    mf.blocks.back().instrs.push_back(
                        MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                    fb.finalize();
                    return mf;
                }
                // cast.fp_to_si.rte.chk: convert to int with fcvtzs and compare round-trip
                if (binI.op == il::core::Opcode::CastFpToSiRteChk)
                {
                    // Load operand to d0
                    const auto &o0 = binI.operands[0];
                    if (o0.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(bb, o0.id);
                        if (pIdx >= 0)
                        {
                            const PhysReg s = ti_->f64ArgOrder[static_cast<std::size_t>(pIdx)];
                            if (s != PhysReg::V0)
                                bbMir.instrs.push_back(MInstr{MOpcode::FMovRR,
                                                              {MOperand::regOp(PhysReg::V0),
                                                               MOperand::regOp(s)}});
                        }
                    }
                    // x0 = fcvtzs d0
                    bbMir.instrs.push_back(MInstr{MOpcode::FCvtZS,
                                                  {MOperand::regOp(PhysReg::X0),
                                                   MOperand::regOp(PhysReg::V0)}});
                    // d1 = scvtf x0; fcmp d0, d1; b.ne trap
                    bbMir.instrs.push_back(MInstr{MOpcode::SCvtF,
                                                  {MOperand::regOp(PhysReg::V1),
                                                   MOperand::regOp(PhysReg::X0)}});
                    bbMir.instrs.push_back(MInstr{MOpcode::FCmpRR,
                                                  {MOperand::regOp(PhysReg::V0),
                                                   MOperand::regOp(PhysReg::V1)}});
                    const std::string trapLabel2 = ".Ltrap_fpcast";
                    bbMir.instrs.push_back(MInstr{MOpcode::BCond,
                                                  {MOperand::condOp("ne"),
                                                   MOperand::labelOp(trapLabel2)}});
                    mf.blocks.emplace_back();
                    mf.blocks.back().name = trapLabel2;
                    mf.blocks.back().instrs.push_back(
                        MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                    fb.finalize();
                    return mf;
                }
            }
            // call @callee(args...) feeding ret
            if (binI.op == il::core::Opcode::Call && retI.op == il::core::Opcode::Ret &&
                binI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result &&
                    !binI.callee.empty())
                {
                    // Use generalized vreg-based lowering when we exceed register args
                    // or when any argument is floating-point.
                    bool hasFloatArg = false;
                    for (const auto &arg : binI.operands)
                    {
                        if (arg.kind == il::core::Value::Kind::ConstFloat)
                        {
                            hasFloatArg = true;
                            break;
                        }
                        if (arg.kind == il::core::Value::Kind::Temp)
                        {
                            int p = indexOfParam(bb, arg.id);
                            if (p >= 0 && p < static_cast<int>(bb.params.size()) &&
                                bb.params[static_cast<std::size_t>(p)].type.kind ==
                                    il::core::Type::Kind::F64)
                            {
                                hasFloatArg = true;
                                break;
                            }
                        }
                    }
                    if (binI.operands.size() > ti_->intArgOrder.size() || hasFloatArg)
                    {
                        LoweredCall seq{};
                        if (lowerCallWithArgs(binI, bb, *ti_, fb, bbMir, seq))
                        {
                            for (auto &mi : seq.prefix)
                                bbMir.instrs.push_back(std::move(mi));
                            bbMir.instrs.push_back(std::move(seq.call));
                            for (auto &mi : seq.postfix)
                                bbMir.instrs.push_back(std::move(mi));
                            fb.finalize();
                            return mf;
                        }
                    }
                    // Single-block, marshal only entry params and const i64 to integer arg regs.
                    const std::size_t nargs = binI.operands.size();
                    if (nargs <= ti_->intArgOrder.size())
                    {
                        // Build move plan for reg->reg moves; immediates applied after.
                        struct Move
                        {
                            PhysReg dst;
                            PhysReg src;
                        };

                        std::vector<Move> moves;
                        std::vector<std::pair<PhysReg, long long>> immLoads;
                        std::vector<std::pair<std::size_t, PhysReg>>
                            tempRegs; // (arg index, reg holding computed temp)
                        const PhysReg scratchPool[] = {PhysReg::X9, PhysReg::X10};
                        std::size_t scratchUsed = 0;
                        auto isParamTemp = [&](const il::core::Value &v, unsigned &outIdx) -> bool
                        {
                            if (v.kind != il::core::Value::Kind::Temp)
                                return false;
                            int p = indexOfParam(bb, v.id);
                            if (p >= 0)
                            {
                                outIdx = static_cast<unsigned>(p);
                                return true;
                            }
                            return false;
                        };
                        auto computeTempTo = [&](const il::core::Instr &prod,
                                                 PhysReg dstReg) -> bool
                        {
                            // Helpers
                            auto rr_emit = [&](MOpcode opc, unsigned p0, unsigned p1)
                            {
                                const PhysReg r0 = argOrder[p0];
                                const PhysReg r1 = argOrder[p1];
                                bbMir.instrs.push_back(MInstr{opc,
                                                              {MOperand::regOp(dstReg),
                                                               MOperand::regOp(r0),
                                                               MOperand::regOp(r1)}});
                            };
                            auto ri_emit = [&](MOpcode opc, unsigned p0, long long imm)
                            {
                                const PhysReg r0 = argOrder[p0];
                                bbMir.instrs.push_back(MInstr{opc,
                                                              {MOperand::regOp(dstReg),
                                                               MOperand::regOp(r0),
                                                               MOperand::immOp(imm)}});
                            };
                            // RR patterns: both operands are entry params
                            if (prod.op == il::core::Opcode::Add ||
                                prod.op == il::core::Opcode::IAddOvf ||
                                prod.op == il::core::Opcode::Sub ||
                                prod.op == il::core::Opcode::ISubOvf ||
                                prod.op == il::core::Opcode::Mul ||
                                prod.op == il::core::Opcode::IMulOvf ||
                                prod.op == il::core::Opcode::And ||
                                prod.op == il::core::Opcode::Or || prod.op == il::core::Opcode::Xor)
                            {
                                if (prod.operands.size() != 2)
                                    return false;
                                if (prod.operands[0].kind == il::core::Value::Kind::Temp &&
                                    prod.operands[1].kind == il::core::Value::Kind::Temp)
                                {
                                    int i0 = indexOfParam(bb, prod.operands[0].id);
                                    int i1 = indexOfParam(bb, prod.operands[1].id);
                                    if (i0 >= 0 && i1 >= 0 && i0 < 8 && i1 < 8)
                                    {
                                        MOpcode opc = MOpcode::AddRRR;
                                        if (prod.op == il::core::Opcode::Add ||
                                            prod.op == il::core::Opcode::IAddOvf)
                                            opc = MOpcode::AddRRR;
                                        else if (prod.op == il::core::Opcode::Sub ||
                                                 prod.op == il::core::Opcode::ISubOvf)
                                            opc = MOpcode::SubRRR;
                                        else if (prod.op == il::core::Opcode::Mul ||
                                                 prod.op == il::core::Opcode::IMulOvf)
                                            opc = MOpcode::MulRRR;
                                        else if (prod.op == il::core::Opcode::And)
                                            opc = MOpcode::AndRRR;
                                        else if (prod.op == il::core::Opcode::Or)
                                            opc = MOpcode::OrrRRR;
                                        else if (prod.op == il::core::Opcode::Xor)
                                            opc = MOpcode::EorRRR;
                                        rr_emit(opc,
                                                static_cast<unsigned>(i0),
                                                static_cast<unsigned>(i1));
                                        return true;
                                    }
                                }
                            }
                            // RI patterns: param + imm for add/sub/shift
                            if (prod.op == il::core::Opcode::Shl ||
                                prod.op == il::core::Opcode::LShr ||
                                prod.op == il::core::Opcode::AShr ||
                                prod.op == il::core::Opcode::Add ||
                                prod.op == il::core::Opcode::IAddOvf ||
                                prod.op == il::core::Opcode::Sub ||
                                prod.op == il::core::Opcode::ISubOvf)
                            {
                                if (prod.operands.size() != 2)
                                    return false;
                                const auto &o0 = prod.operands[0];
                                const auto &o1 = prod.operands[1];
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int ip = indexOfParam(bb, o0.id);
                                    if (ip >= 0 && ip < 8)
                                    {
                                        if (prod.op == il::core::Opcode::Shl)
                                            ri_emit(
                                                MOpcode::LslRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::LShr)
                                            ri_emit(
                                                MOpcode::LsrRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::AShr)
                                            ri_emit(
                                                MOpcode::AsrRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::Add ||
                                                 prod.op == il::core::Opcode::IAddOvf)
                                            ri_emit(
                                                MOpcode::AddRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::Sub ||
                                                 prod.op == il::core::Opcode::ISubOvf)
                                            ri_emit(
                                                MOpcode::SubRI, static_cast<unsigned>(ip), o1.i64);
                                        return true;
                                    }
                                }
                                else if (o1.kind == il::core::Value::Kind::Temp &&
                                         o0.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int ip = indexOfParam(bb, o1.id);
                                    if (ip >= 0 && ip < 8)
                                    {
                                        if (prod.op == il::core::Opcode::Shl)
                                            ri_emit(
                                                MOpcode::LslRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::LShr)
                                            ri_emit(
                                                MOpcode::LsrRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::AShr)
                                            ri_emit(
                                                MOpcode::AsrRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::Add ||
                                                 prod.op == il::core::Opcode::IAddOvf)
                                            ri_emit(
                                                MOpcode::AddRI, static_cast<unsigned>(ip), o0.i64);
                                        // Sub with const first not supported
                                        return true;
                                    }
                                }
                            }
                            // Compare patterns: produce 0/1 in dstReg via cmp + cset
                            if (prod.op == il::core::Opcode::ICmpEq ||
                                prod.op == il::core::Opcode::ICmpNe ||
                                prod.op == il::core::Opcode::SCmpLT ||
                                prod.op == il::core::Opcode::SCmpLE ||
                                prod.op == il::core::Opcode::SCmpGT ||
                                prod.op == il::core::Opcode::SCmpGE ||
                                prod.op == il::core::Opcode::UCmpLT ||
                                prod.op == il::core::Opcode::UCmpLE ||
                                prod.op == il::core::Opcode::UCmpGT ||
                                prod.op == il::core::Opcode::UCmpGE)
                            {
                                if (prod.operands.size() != 2)
                                    return false;
                                const auto &o0 = prod.operands[0];
                                const auto &o1 = prod.operands[1];
                                const char *cc = condForOpcode(prod.op);
                                if (!cc)
                                    return false;
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::Temp)
                                {
                                    int i0 = indexOfParam(bb, o0.id);
                                    int i1 = indexOfParam(bb, o1.id);
                                    if (i0 >= 0 && i1 >= 0 && i0 < 8 && i1 < 8)
                                    {
                                        const PhysReg r0 = argOrder[i0];
                                        const PhysReg r1 = argOrder[i1];
                                        bbMir.instrs.push_back(
                                            MInstr{MOpcode::CmpRR,
                                                   {MOperand::regOp(r0), MOperand::regOp(r1)}});
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::Cset,
                                            {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
                                        return true;
                                    }
                                }
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int i0 = indexOfParam(bb, o0.id);
                                    if (i0 >= 0 && i0 < 8)
                                    {
                                        const PhysReg r0 = argOrder[i0];
                                        // cmp r0, #imm; cset dst, cc
                                        bbMir.instrs.push_back(
                                            MInstr{MOpcode::CmpRI,
                                                   {MOperand::regOp(r0), MOperand::immOp(o1.i64)}});
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::Cset,
                                            {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
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
                                    if (arg.kind == il::core::Value::Kind::Temp &&
                                        scratchUsed <
                                            (sizeof(scratchPool) / sizeof(scratchPool[0])))
                                    {
                                        auto it = std::find_if(
                                            bb.instructions.begin(),
                                            bb.instructions.end(),
                                            [&](const il::core::Instr &I)
                                            { return I.result && *I.result == arg.id; });
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
                                    supported = false;
                                    break; // unsupported temp
                                }
                            }
                        }
                        if (!supported)
                        { /* fallthrough: no call lowering */
                        }
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
                            auto hasDst = [&](PhysReg r)
                            {
                                for (auto &m : moves)
                                    if (m.dst == r)
                                        return true;
                                return false;
                            };
                            // Perform until empty
                            while (!moves.empty())
                            {
                                bool progressed = false;
                                for (auto it = moves.begin(); it != moves.end();)
                                {
                                    if (!hasDst(it->src))
                                    {
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::MovRR,
                                            {MOperand::regOp(it->dst), MOperand::regOp(it->src)}});
                                        it = moves.erase(it);
                                        progressed = true;
                                    }
                                    else
                                    {
                                        ++it;
                                    }
                                }
                                if (!progressed)
                                {
                                    const PhysReg cycleSrc = moves.front().src;
                                    bbMir.instrs.push_back(MInstr{
                                        MOpcode::MovRR,
                                        {MOperand::regOp(PhysReg::X9), MOperand::regOp(cycleSrc)}});
                                    for (auto &m : moves)
                                        if (m.src == cycleSrc)
                                            m.src = PhysReg::X9;
                                }
                            }
                            // Apply immediates
                            for (auto &pr : immLoads)
                                bbMir.instrs.push_back(MInstr{
                                    MOpcode::MovRI,
                                    {MOperand::regOp(pr.first), MOperand::immOp(pr.second)}});

                            // Stack args: allocate area, materialize values, store at [sp, #offset]
                            if (nStackArgs > 0)
                            {
                                long long frameBytes = static_cast<long long>(nStackArgs) * 8LL;
                                if (frameBytes % 16LL != 0LL)
                                    frameBytes += 8LL; // 16-byte alignment
                                // Reserve in frame builder, do not emit dynamic SP adjust
                                fb.setMaxOutgoingBytes(static_cast<int>(frameBytes));
                                for (std::size_t i = nReg; i < nargs; ++i)
                                {
                                    const auto &arg = binI.operands[i];
                                    PhysReg valReg = PhysReg::X9;
                                    if (arg.kind == il::core::Value::Kind::ConstInt)
                                    {
                                        // Use a scratch reg to hold the constant
                                        const PhysReg tmp = (scratchUsed < (sizeof(scratchPool) /
                                                                            sizeof(scratchPool[0])))
                                                                ? scratchPool[scratchUsed++]
                                                                : PhysReg::X9;
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::MovRI,
                                            {MOperand::regOp(tmp), MOperand::immOp(arg.i64)}});
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
                                            if (scratchUsed >=
                                                (sizeof(scratchPool) / sizeof(scratchPool[0])))
                                            {
                                                supported = false;
                                                break;
                                            }
                                            valReg = scratchPool[scratchUsed++];
                                            auto it = std::find_if(
                                                bb.instructions.begin(),
                                                bb.instructions.end(),
                                                [&](const il::core::Instr &I)
                                                { return I.result && *I.result == arg.id; });
                                            if (it == bb.instructions.end() ||
                                                !computeTempTo(*it, valReg))
                                            {
                                                supported = false;
                                                break;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        supported = false;
                                        break;
                                    }
                                    const long long off = static_cast<long long>((i - nReg) * 8ULL);
                                    bbMir.instrs.push_back(
                                        MInstr{MOpcode::StrRegSpImm,
                                               {MOperand::regOp(valReg), MOperand::immOp(off)}});
                                }
                                if (!supported)
                                {
                                    // If unsupported mid-way, do not emit call sequence.
                                    fb.finalize();
                                    return mf;
                                }
                                // Emit call
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
                                fb.finalize();
                                return mf;
                            }
                            // No stack args; emit call directly
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
                            fb.finalize();
                            return mf;
                        }
                    }
                }
            }
            const bool isAdd =
                (binI.op == il::core::Opcode::Add || binI.op == il::core::Opcode::IAddOvf);
            const bool isSub =
                (binI.op == il::core::Opcode::Sub || binI.op == il::core::Opcode::ISubOvf);
            const bool isShl = (binI.op == il::core::Opcode::Shl);
            const bool isLShr = (binI.op == il::core::Opcode::LShr);
            const bool isAShr = (binI.op == il::core::Opcode::AShr);
            const bool isICmpImm = (condForOpcode(binI.op) != nullptr);
            if ((isAdd || isSub || isShl || isLShr || isAShr) && retI.op == il::core::Opcode::Ret &&
                binI.result && !retI.operands.empty() && binI.operands.size() == 2)
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                {
                    const auto &o0 = binI.operands[0];
                    const auto &o1 = binI.operands[1];
                    auto emitImm = [&](unsigned paramIndex, long long imm)
                    {
                        const PhysReg src = argOrder[paramIndex];
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        if (isAdd)
                            bbMir.instrs.push_back(MInstr{MOpcode::AddRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isSub)
                            bbMir.instrs.push_back(MInstr{MOpcode::SubRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isShl)
                            bbMir.instrs.push_back(MInstr{MOpcode::LslRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isLShr)
                            bbMir.instrs.push_back(MInstr{MOpcode::LsrRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isAShr)
                            bbMir.instrs.push_back(MInstr{MOpcode::AsrRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                    };
                    if (o0.kind == il::core::Value::Kind::Temp &&
                        o1.kind == il::core::Value::Kind::ConstInt)
                    {
                        for (size_t i = 0; i < bb.params.size(); ++i)
                            if (bb.params[i].id == o0.id && i < 8)
                            {
                                emitImm(static_cast<unsigned>(i), o1.i64);
                                return mf;
                            }
                    }
                    if (o1.kind == il::core::Value::Kind::Temp &&
                        o0.kind == il::core::Value::Kind::ConstInt)
                    {
                        if (isAdd || isShl || isLShr || isAShr)
                        {
                            for (size_t i = 0; i < bb.params.size(); ++i)
                                if (bb.params[i].id == o1.id && i < 8)
                                {
                                    emitImm(static_cast<unsigned>(i), o0.i64);
                                    return mf;
                                }
                        }
                    }
                }
            }
            if (isICmpImm && retI.op == il::core::Opcode::Ret && binI.result &&
                !retI.operands.empty() && binI.operands.size() == 2)
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
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::MovRR,
                                       {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::CmpRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::Cset,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::condOp(condForOpcode(binI.op))}});
                    };
                    if (o0.kind == il::core::Value::Kind::Temp &&
                        o1.kind == il::core::Value::Kind::ConstInt)
                    {
                        for (size_t i = 0; i < bb.params.size(); ++i)
                            if (bb.params[i].id == o0.id && i < 8)
                            {
                                emitCmpImm(static_cast<unsigned>(i), o1.i64);
                                return mf;
                            }
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
                        bbOut(0).instrs.push_back(MInstr{
                            MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        return mf;
                    }
                }
            }
        }

        // Lower simple control-flow terminators: br and cbr
        for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        {
            const auto &inBB = fn.blocks[i];
            if (inBB.instructions.empty())
                continue;
            const auto &term = inBB.instructions.back();
            auto &outBB = bbOut(i);
            switch (term.op)
            {
                case il::core::Opcode::Br:
                    if (!term.labels.empty())
                        outBB.instrs.push_back(
                            MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[0])}});
                    break;
                case il::core::Opcode::Trap:
                {
                    // Phase A: lower trap to a helper call for diagnostics.
                    outBB.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                    break;
                }
                case il::core::Opcode::TrapFromErr:
                {
                    // Phase A: move optional error code into x0 (when available), then call rt_trap.
                    if (!term.operands.empty())
                    {
                        const auto &code = term.operands[0];
                        if (code.kind == il::core::Value::Kind::ConstInt)
                        {
                            outBB.instrs.push_back(MInstr{MOpcode::MovRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(code.i64)}});
                        }
                        else if (code.kind == il::core::Value::Kind::Temp)
                        {
                            int pIdx = indexOfParam(inBB, code.id);
                            if (pIdx >= 0 && pIdx < 8)
                            {
                                const PhysReg src = argOrder[static_cast<std::size_t>(pIdx)];
                                if (src != PhysReg::X0)
                                    outBB.instrs.push_back(MInstr{MOpcode::MovRR,
                                                                  {MOperand::regOp(PhysReg::X0),
                                                                   MOperand::regOp(src)}});
                            }
                        }
                    }
                    outBB.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                    break;
                }
                case il::core::Opcode::SwitchI32:
                {
                    // Lower as a chain of cmp+beq then branch to default.
                    const auto &scrut = il::core::switchScrutinee(term);

                    // Materialize scrutinee into a register suitable for compare.
                    // Prefer using entry param phys regs when possible; otherwise materialize
                    // into a vreg and let regalloc assign a physical register.
                    std::unordered_map<unsigned, uint16_t> tmp2vreg; // IL temp -> vreg
                    uint16_t nextVRegId = 1;
                    bool haveReg = false;
                    bool usePhysX0 = false;
                    uint16_t scrVReg = 0;
                    if (scrut.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(inBB, scrut.id);
                        if (pIdx >= 0 && pIdx < static_cast<int>(argOrder.size()))
                        {
                            const PhysReg src = argOrder[static_cast<std::size_t>(pIdx)];
                            if (src != PhysReg::X0)
                            {
                                outBB.instrs.push_back(MInstr{MOpcode::MovRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(src)}});
                            }
                            haveReg = true;
                            usePhysX0 = true;
                        }
                        else
                        {
                            uint16_t vreg = 0;
                            RegClass cls = RegClass::GPR;
                            if (materializeValueToVReg(scrut, inBB, *ti_, fb, outBB, tmp2vreg,
                                                       nextVRegId, vreg, cls) &&
                                cls == RegClass::GPR)
                            {
                                scrVReg = vreg;
                                haveReg = true;
                            }
                        }
                    }
                    else if (scrut.kind == il::core::Value::Kind::ConstInt)
                    {
                        // Move constant into x0 for compare immediates.
                        outBB.instrs.push_back(MInstr{MOpcode::MovRI,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::immOp(scrut.i64)}});
                        haveReg = true;
                        usePhysX0 = true;
                    }

                    // Emit compare branches for each case
                    const std::size_t ncases = il::core::switchCaseCount(term);
                    for (std::size_t ci = 0; ci < ncases; ++ci)
                    {
                        const auto &cv = il::core::switchCaseValue(term, ci);
                        const auto &clbl = il::core::switchCaseLabel(term, ci);
                        if (cv.kind == il::core::Value::Kind::ConstInt)
                        {
                            if (!haveReg)
                            {
                                // As a last resort (shouldn't happen for valid switch.i32),
                                // treat scrutinee as coming from x0.
                                usePhysX0 = true;
                            }
                            if (usePhysX0)
                            {
                                outBB.instrs.push_back(MInstr{MOpcode::CmpRI,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::immOp(cv.i64)}});
                            }
                            else
                            {
                                outBB.instrs.push_back(MInstr{MOpcode::CmpRI,
                                                              {MOperand::vregOp(RegClass::GPR,
                                                                                scrVReg),
                                                               MOperand::immOp(cv.i64)}});
                            }
                            outBB.instrs.push_back(MInstr{MOpcode::BCond,
                                                          {MOperand::condOp("eq"),
                                                           MOperand::labelOp(clbl)}});
                        }
                    }
                    // Default
                    outBB.instrs.push_back(MInstr{MOpcode::Br,
                                                  {MOperand::labelOp(
                                                      il::core::switchDefaultLabel(term))}});
                    break;
                }
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
                                const auto it =
                                    std::find_if(inBB.instructions.begin(),
                                                 inBB.instructions.end(),
                                                 [&](const il::core::Instr &I)
                                                 { return I.result && *I.result == cond.id; });
                                if (it != inBB.instructions.end())
                                {
                                    const il::core::Instr &cmpI = *it;
                                    const char *cc = condForOpcode(cmpI.op);
                                    if (cc && cmpI.operands.size() == 2)
                                    {
                                        const auto &o0 = cmpI.operands[0];
                                        const auto &o1 = cmpI.operands[1];
                                        if (o0.kind == il::core::Value::Kind::Temp &&
                                            o1.kind == il::core::Value::Kind::Temp)
                                        {
                                            int idx0 = indexOfParam(inBB, o0.id);
                                            int idx1 = indexOfParam(inBB, o1.id);
                                            if (idx0 >= 0 && idx1 >= 0 && idx0 < 8 && idx1 < 8)
                                            {
                                                const PhysReg src0 =
                                                    argOrder[static_cast<size_t>(idx0)];
                                                const PhysReg src1 =
                                                    argOrder[static_cast<size_t>(idx1)];
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::MovRR,
                                                           {MOperand::regOp(PhysReg::X9),
                                                            MOperand::regOp(src1)}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::MovRR,
                                                           {MOperand::regOp(PhysReg::X0),
                                                            MOperand::regOp(src0)}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::MovRR,
                                                           {MOperand::regOp(PhysReg::X1),
                                                            MOperand::regOp(PhysReg::X9)}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::CmpRR,
                                                           {MOperand::regOp(PhysReg::X0),
                                                            MOperand::regOp(PhysReg::X1)}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::BCond,
                                                           {MOperand::condOp(cc),
                                                            MOperand::labelOp(term.labels[0])}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::Br,
                                                           {MOperand::labelOp(term.labels[1])}});
                                                loweredViaCompare = true;
                                            }
                                        }
                                        else if (o0.kind == il::core::Value::Kind::Temp &&
                                                 o1.kind == il::core::Value::Kind::ConstInt)
                                        {
                                            int idx0 = indexOfParam(inBB, o0.id);
                                            if (idx0 >= 0 && idx0 < 8)
                                            {
                                                const PhysReg src0 =
                                                    argOrder[static_cast<size_t>(idx0)];
                                                if (src0 != PhysReg::X0)
                                                    outBB.instrs.push_back(
                                                        MInstr{MOpcode::MovRR,
                                                               {MOperand::regOp(PhysReg::X0),
                                                                MOperand::regOp(src0)}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::CmpRI,
                                                           {MOperand::regOp(PhysReg::X0),
                                                            MOperand::immOp(o1.i64)}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::BCond,
                                                           {MOperand::condOp(cc),
                                                            MOperand::labelOp(term.labels[0])}});
                                                outBB.instrs.push_back(
                                                    MInstr{MOpcode::Br,
                                                           {MOperand::labelOp(term.labels[1])}});
                                                loweredViaCompare = true;
                                            }
                                        }
                                    }
                                }
                            }
                            if (!loweredViaCompare)
                            {
                                // Fallback: assume cond in x0 (or move param to x0), then cmp x0,
                                // #0; b.ne true; b false
                                if (cond.kind == il::core::Value::Kind::Temp)
                                {
                                    int pIdx = indexOfParam(inBB, cond.id);
                                    if (pIdx >= 0)
                                    {
                                        const PhysReg src = argOrder[static_cast<size_t>(pIdx)];
                                        if (src != PhysReg::X0)
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::MovRR,
                                                       {MOperand::regOp(PhysReg::X0),
                                                        MOperand::regOp(src)}});
                                    }
                                }
                                outBB.instrs.push_back(
                                    MInstr{MOpcode::CmpRI,
                                           {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
                                outBB.instrs.push_back(MInstr{
                                    MOpcode::BCond,
                                    {MOperand::condOp("ne"), MOperand::labelOp(term.labels[0])}});
                                outBB.instrs.push_back(
                                    MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[1])}});
                            }
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }
    fb.finalize();
    return mf;
}

} // namespace viper::codegen::aarch64
