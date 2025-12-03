//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/InstrLowering.cpp
// Purpose: Opcode-specific lowering handlers for IL->MIR conversion.
//
// This file contains extracted handler functions for individual IL opcodes,
// reducing the size of LowerILToMIR.cpp and improving maintainability.
//
//===----------------------------------------------------------------------===//

#include "InstrLowering.hpp"
#include "FrameBuilder.hpp"
#include "OpcodeMappings.hpp"

#include <cstring>

namespace viper::codegen::aarch64
{

//===----------------------------------------------------------------------===//
// Thread-local temp registry for FPR tracking
//===----------------------------------------------------------------------===//

// Maps IL temp id to its register class (GPR or FPR).
// This must be shared with LowerILToMIR.cpp, so we declare it extern there
// and define it here.
thread_local std::unordered_map<unsigned, RegClass> g_tempRegClass;

//===----------------------------------------------------------------------===//
// Helper: Get condition code for comparison opcodes
//===----------------------------------------------------------------------===//

static const char *condForOpcode(il::core::Opcode op)
{
    return lookupCondition(op);
}

static const char *fpCondCode(il::core::Opcode op)
{
    switch (op)
    {
        case il::core::Opcode::FCmpEQ:
            return "eq";
        case il::core::Opcode::FCmpNE:
            return "ne";
        case il::core::Opcode::FCmpLT:
            return "mi"; // mi = negative, used for ordered <
        case il::core::Opcode::FCmpLE:
            return "ls"; // ls = lower or same
        case il::core::Opcode::FCmpGT:
            return "gt";
        case il::core::Opcode::FCmpGE:
            return "ge";
        default:
            return "eq";
    }
}

//===----------------------------------------------------------------------===//
// Value Materialization
//===----------------------------------------------------------------------===//

bool materializeValueToVReg(const il::core::Value &v,
                            const il::core::BasicBlock &bb,
                            const TargetInfo &ti,
                            FrameBuilder &fb,
                            MBasicBlock &out,
                            std::unordered_map<unsigned, uint16_t> &tempVReg,
                            uint16_t &nextVRegId,
                            uint16_t &outVReg,
                            RegClass &outCls)
{
    using Opcode = il::core::Opcode;

    if (v.kind == il::core::Value::Kind::ConstInt)
    {
        outVReg = nextVRegId++;
        outCls = RegClass::GPR;
        out.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::vregOp(outCls, outVReg), MOperand::immOp(v.i64)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::ConstFloat)
    {
        // Materialize FP constant by moving its bit-pattern via a GPR into an FPR.
        long long bits;
        static_assert(sizeof(double) == sizeof(long long), "size");
        std::memcpy(&bits, &v.f64, sizeof(double));
        const uint16_t tmpG = nextVRegId++;
        // Load 64-bit pattern into a GPR vreg
        out.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, tmpG), MOperand::immOp(bits)}});
        outVReg = nextVRegId++;
        outCls = RegClass::FPR;
        // fmov dV, xTmp  (bit-cast)
        out.instrs.push_back(MInstr{MOpcode::FMovRR,
                                    {MOperand::vregOp(RegClass::FPR, outVReg),
                                     MOperand::vregOp(RegClass::GPR, tmpG)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::NullPtr)
    {
        // Null pointer is just immediate 0
        outVReg = nextVRegId++;
        outCls = RegClass::GPR;
        out.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::vregOp(outCls, outVReg), MOperand::immOp(0)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::Temp)
    {
        // First check if we already materialized this temp (includes block params
        // loaded from spill slots in non-entry blocks)
        auto it = tempVReg.find(v.id);
        if (it != tempVReg.end())
        {
            outVReg = it->second;
            // Look up register class for this temp
            auto clsIt = g_tempRegClass.find(v.id);
            outCls = (clsIt != g_tempRegClass.end()) ? clsIt->second : RegClass::GPR;
            return true;
        }
        // Check if this is an alloca temp - if so, compute its stack address
        // This must be checked before the instruction search since allocas are
        // defined in the entry block but used in other blocks.
        // Note: We don't cache the result in tempVReg because the vreg->phys mapping
        // changes across blocks, and we need to recompute the address each time.
        const int allocaOff = fb.localOffset(v.id);
        if (allocaOff != 0)
        {
            outVReg = nextVRegId++;
            outCls = RegClass::GPR;
            out.instrs.push_back(MInstr{
                MOpcode::AddFpImm,
                {MOperand::vregOp(RegClass::GPR, outVReg), MOperand::immOp(allocaOff)}});
            // Don't cache: tempVReg[v.id] = outVReg;
            return true;
        }
        // If it's a function entry param (in entry block), move from ABI phys -> vreg.
        // This only applies to entry block parameters, not block parameters in other blocks.
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
                out.instrs.push_back(
                    MInstr{MOpcode::MovRR, {MOperand::vregOp(cls, outVReg), MOperand::regOp(src)}});
            }
            else
            {
                const PhysReg src = ti.f64ArgOrder[static_cast<std::size_t>(pIdx)];
                out.instrs.push_back(MInstr{
                    MOpcode::FMovRR, {MOperand::vregOp(cls, outVReg), MOperand::regOp(src)}});
            }
            return true;
        }
        // Find the producing instruction within the block and lower a subset
        auto prodIt =
            std::find_if(bb.instructions.begin(),
                         bb.instructions.end(),
                         [&](const il::core::Instr &I) { return I.result && *I.result == v.id; });
        if (prodIt == bb.instructions.end())
            return false;

        auto emitRRR = [&](MOpcode opc, const il::core::Value &a, const il::core::Value &b) -> bool
        {
            uint16_t va = 0, vb = 0;
            RegClass ca = RegClass::GPR, cb = RegClass::GPR;
            if (!materializeValueToVReg(a, bb, ti, fb, out, tempVReg, nextVRegId, va, ca))
                return false;
            if (!materializeValueToVReg(b, bb, ti, fb, out, tempVReg, nextVRegId, vb, cb))
                return false;
            outVReg = nextVRegId++;
            outCls = (opc == MOpcode::FAddRRR || opc == MOpcode::FSubRRR ||
                      opc == MOpcode::FMulRRR || opc == MOpcode::FDivRRR)
                         ? RegClass::FPR
                         : RegClass::GPR;
            out.instrs.push_back(MInstr{opc,
                                        {MOperand::vregOp(outCls, outVReg),
                                         MOperand::vregOp(outCls, va),
                                         MOperand::vregOp(outCls, vb)}});
            return true;
        };
        auto emitRImm = [&](MOpcode opc, const il::core::Value &a, long long imm) -> bool
        {
            uint16_t va = 0;
            RegClass ca = RegClass::GPR;
            if (!materializeValueToVReg(a, bb, ti, fb, out, tempVReg, nextVRegId, va, ca))
                return false;
            outVReg = nextVRegId++;
            outCls = (opc == MOpcode::FAddRRR || opc == MOpcode::FSubRRR ||
                      opc == MOpcode::FMulRRR || opc == MOpcode::FDivRRR)
                         ? RegClass::FPR
                         : RegClass::GPR;
            out.instrs.push_back(MInstr{opc,
                                        {MOperand::vregOp(outCls, outVReg),
                                         MOperand::vregOp(outCls, va),
                                         MOperand::immOp(imm)}});
            return true;
        };

        const auto &prod = *prodIt;

        // Check for binary operations first using table lookup
        if (const auto *binOp = lookupBinaryOp(prod.op))
        {
            if (prod.operands.size() == 2)
            {
                // Check if this is a shift operation that requires immediate
                bool isShift =
                    (prod.op == Opcode::Shl || prod.op == Opcode::LShr || prod.op == Opcode::AShr);

                if (binOp->supportsImmediate &&
                    prod.operands[1].kind == il::core::Value::Kind::ConstInt)
                {
                    if (emitRImm(binOp->immOp, prod.operands[0], prod.operands[1].i64))
                    {
                        // Cache result to prevent re-materialization with different vreg
                        tempVReg[v.id] = outVReg;
                        return true;
                    }
                    return false;
                }
                else if (!isShift)
                {
                    // Non-shift operations can use register-register form
                    if (emitRRR(binOp->mirOp, prod.operands[0], prod.operands[1]))
                    {
                        // Cache result to prevent re-materialization with different vreg
                        tempVReg[v.id] = outVReg;
                        return true;
                    }
                    return false;
                }
            }
        }

        // Handle other operations
        switch (prod.op)
        {
            case Opcode::ConstStr:
                if (!prod.operands.empty() &&
                    prod.operands[0].kind == il::core::Value::Kind::GlobalAddr)
                {
                    // Materialize address of pooled literal label into a temp GPR
                    const uint16_t litPtrV = nextVRegId++;
                    const std::string &sym = prod.operands[0].str;
                    out.instrs.push_back(
                        MInstr{MOpcode::AdrPage,
                               {MOperand::vregOp(RegClass::GPR, litPtrV), MOperand::labelOp(sym)}});
                    out.instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                {MOperand::vregOp(RegClass::GPR, litPtrV),
                                                 MOperand::vregOp(RegClass::GPR, litPtrV),
                                                 MOperand::labelOp(sym)}});

                    // Call rt_const_cstr(litPtr) to obtain an rt_string handle in x0
                    out.instrs.push_back(MInstr{
                        MOpcode::MovRR,
                        {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, litPtrV)}});
                    out.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_const_cstr")}});

                    // Move x0 (rt_string) into a fresh vreg as the const_str result
                    outVReg = nextVRegId++;
                    outCls = RegClass::GPR;
                    out.instrs.push_back(MInstr{
                        MOpcode::MovRR,
                        {MOperand::vregOp(RegClass::GPR, outVReg), MOperand::regOp(PhysReg::X0)}});
                    // Cache for reuse
                    tempVReg[v.id] = outVReg;
                    return true;
                }
                break;
            case Opcode::AddrOf:
                if (!prod.operands.empty() &&
                    prod.operands[0].kind == il::core::Value::Kind::GlobalAddr)
                {
                    outVReg = nextVRegId++;
                    outCls = RegClass::GPR;
                    const std::string &sym = prod.operands[0].str;
                    out.instrs.push_back(
                        MInstr{MOpcode::AdrPage,
                               {MOperand::vregOp(RegClass::GPR, outVReg), MOperand::labelOp(sym)}});
                    out.instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                {MOperand::vregOp(RegClass::GPR, outVReg),
                                                 MOperand::vregOp(RegClass::GPR, outVReg),
                                                 MOperand::labelOp(sym)}});
                    tempVReg[v.id] = outVReg;
                    return true;
                }
                break;
            case Opcode::GEP:
                if (prod.operands.size() >= 2)
                {
                    uint16_t vbase = 0, voff = 0;
                    RegClass cbase = RegClass::GPR, coff = RegClass::GPR;
                    if (!materializeValueToVReg(
                            prod.operands[0], bb, ti, fb, out, tempVReg, nextVRegId, vbase, cbase))
                        return false;
                    outVReg = nextVRegId++;
                    outCls = RegClass::GPR;
                    const auto &offVal = prod.operands[1];
                    if (offVal.kind == il::core::Value::Kind::ConstInt)
                    {
                        const long long imm = offVal.i64;
                        if (imm == 0)
                        {
                            out.instrs.push_back(MInstr{MOpcode::MovRR,
                                                        {MOperand::vregOp(RegClass::GPR, outVReg),
                                                         MOperand::vregOp(RegClass::GPR, vbase)}});
                        }
                        else
                        {
                            out.instrs.push_back(MInstr{MOpcode::AddRI,
                                                        {MOperand::vregOp(RegClass::GPR, outVReg),
                                                         MOperand::vregOp(RegClass::GPR, vbase),
                                                         MOperand::immOp(imm)}});
                        }
                    }
                    else
                    {
                        if (!materializeValueToVReg(
                                offVal, bb, ti, fb, out, tempVReg, nextVRegId, voff, coff))
                            return false;
                        out.instrs.push_back(MInstr{MOpcode::AddRRR,
                                                    {MOperand::vregOp(RegClass::GPR, outVReg),
                                                     MOperand::vregOp(RegClass::GPR, vbase),
                                                     MOperand::vregOp(RegClass::GPR, voff)}});
                    }
                    tempVReg[v.id] = outVReg;
                    return true;
                }
                break;
            default:
                // Check if it's a comparison operation
                if (isCompareOp(prod.op))
                {
                    if (prod.operands.size() == 2)
                    {
                        uint16_t va = 0, vb = 0;
                        RegClass ca = RegClass::GPR, cb = RegClass::GPR;
                        if (!materializeValueToVReg(
                                prod.operands[0], bb, ti, fb, out, tempVReg, nextVRegId, va, ca))
                            return false;
                        if (!materializeValueToVReg(
                                prod.operands[1], bb, ti, fb, out, tempVReg, nextVRegId, vb, cb))
                            return false;
                        out.instrs.push_back(MInstr{MOpcode::CmpRR,
                                                    {MOperand::vregOp(RegClass::GPR, va),
                                                     MOperand::vregOp(RegClass::GPR, vb)}});
                        outVReg = nextVRegId++;
                        outCls = RegClass::GPR;
                        out.instrs.push_back(MInstr{MOpcode::Cset,
                                                    {MOperand::vregOp(RegClass::GPR, outVReg),
                                                     MOperand::condOp(condForOpcode(prod.op))}});
                        // Cache result to prevent re-materialization with different vreg
                        tempVReg[v.id] = outVReg;
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
                        out.instrs.push_back(
                            MInstr{MOpcode::LdrRegFpImm,
                                   {MOperand::vregOp(outCls, outVReg), MOperand::immOp(off)}});
                        return true;
                    }
                }
                break;
        }
    }
    return false;
}

//===----------------------------------------------------------------------===//
// Call Lowering
//===----------------------------------------------------------------------===//

bool lowerCallWithArgs(const il::core::Instr &callI,
                       const il::core::BasicBlock &bb,
                       const TargetInfo &ti,
                       FrameBuilder &fb,
                       MBasicBlock &out,
                       LoweredCall &seq,
                       std::unordered_map<unsigned, uint16_t> &tempVReg,
                       uint16_t &nextVRegId)
{
    // Callee can be in either callI.callee field or operands[0] as GlobalAddr
    std::string callee;
    std::size_t argStart = 0;

    if (!callI.callee.empty())
    {
        // Modern IL convention: callee in dedicated field, all operands are arguments
        callee = callI.callee;
        argStart = 0;
    }
    else if (!callI.operands.empty() && callI.operands[0].kind == il::core::Value::Kind::GlobalAddr)
    {
        // Legacy convention: callee as GlobalAddr in operands[0]
        callee = callI.operands[0].str;
        argStart = 1;
    }
    else
    {
        return false;
    }

    seq.call = MInstr{MOpcode::Bl, {MOperand::labelOp(callee)}};

    // First pass: materialize all arguments and collect them
    struct ArgInfo
    {
        uint16_t vreg;
        RegClass cls;
    };
    std::vector<ArgInfo> args;
    for (std::size_t i = argStart; i < callI.operands.size(); ++i)
    {
        const auto &arg = callI.operands[i];
        uint16_t vr = 0;
        RegClass cls = RegClass::GPR;
        if (!materializeValueToVReg(arg, bb, ti, fb, out, tempVReg, nextVRegId, vr, cls))
            return false;
        args.push_back({vr, cls});
    }

    // Count how many stack slots we need (args beyond register capacity)
    std::size_t gprIdx = 0;
    std::size_t fprIdx = 0;
    std::size_t stackSlots = 0;
    for (const auto &a : args)
    {
        if (a.cls == RegClass::FPR)
        {
            if (fprIdx < ti.f64ArgOrder.size())
                fprIdx++;
            else
                stackSlots++;
        }
        else
        {
            if (gprIdx < ti.intArgOrder.size())
                gprIdx++;
            else
                stackSlots++;
        }
    }

    // If we have stack args, allocate space on stack (16-byte aligned)
    const std::size_t stackBytes = ((stackSlots * 8 + 15) / 16) * 16;
    if (stackBytes > 0)
    {
        seq.prefix.push_back(
            MInstr{MOpcode::SubSpImm,
                   {MOperand::immOp(static_cast<long long>(stackBytes))}});
    }

    // Second pass: marshal arguments into registers or stack slots
    gprIdx = 0;
    fprIdx = 0;
    std::size_t stackOffset = 0;
    for (const auto &a : args)
    {
        if (a.cls == RegClass::FPR)
        {
            if (fprIdx < ti.f64ArgOrder.size())
            {
                PhysReg dst = ti.f64ArgOrder[fprIdx++];
                seq.prefix.push_back(MInstr{MOpcode::FMovRR,
                                            {MOperand::regOp(dst),
                                             MOperand::vregOp(RegClass::FPR, a.vreg)}});
            }
            else
            {
                // Spill to stack - use str for FPR to [sp, #offset]
                seq.prefix.push_back(
                    MInstr{MOpcode::StrFprSpImm,
                           {MOperand::vregOp(RegClass::FPR, a.vreg),
                            MOperand::immOp(static_cast<long long>(stackOffset))}});
                stackOffset += 8;
            }
        }
        else
        {
            if (gprIdx < ti.intArgOrder.size())
            {
                PhysReg dst = ti.intArgOrder[gprIdx++];
                seq.prefix.push_back(MInstr{MOpcode::MovRR,
                                            {MOperand::regOp(dst),
                                             MOperand::vregOp(RegClass::GPR, a.vreg)}});
            }
            else
            {
                // Spill to stack - use str for GPR to [sp, #offset]
                seq.prefix.push_back(
                    MInstr{MOpcode::StrRegSpImm,
                           {MOperand::vregOp(RegClass::GPR, a.vreg),
                            MOperand::immOp(static_cast<long long>(stackOffset))}});
                stackOffset += 8;
            }
        }
    }

    // After the call, deallocate stack space
    if (stackBytes > 0)
    {
        seq.postfix.push_back(
            MInstr{MOpcode::AddSpImm,
                   {MOperand::immOp(static_cast<long long>(stackBytes))}});
    }

    return true;
}

//===----------------------------------------------------------------------===//
// Signed Remainder with Divide-by-Zero Check (BUG-ARM-010 fix)
//===----------------------------------------------------------------------===//

bool lowerSRemChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out)
{
    if (!ins.result || ins.operands.size() < 2)
        return false;

    // Materialize lhs and rhs
    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::GPR, rhsCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, lhs, lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, rhs, rhsCls))
        return false;

    // Generate divide-by-zero check: cmp rhs, #0; b.eq trap_label
    // Note: cbz has limited range and requires local labels, so we use cmp+b.eq
    const std::string trapLabel = ".Ltrap_div0_" + std::to_string(ctx.trapLabelCounter++);
    out.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, rhs), MOperand::immOp(0)}});
    out.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(trapLabel)}});

    // Create trap block
    ctx.mf.blocks.emplace_back();
    ctx.mf.blocks.back().name = trapLabel;
    ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});

    // Compute quotient: sdiv tmp, lhs, rhs
    const uint16_t quotient = ctx.nextVRegId++;
    out.instrs.push_back(MInstr{MOpcode::SDivRRR,
                                {MOperand::vregOp(RegClass::GPR, quotient),
                                 MOperand::vregOp(RegClass::GPR, lhs),
                                 MOperand::vregOp(RegClass::GPR, rhs)}});

    // Compute remainder: msub dst, quotient, rhs, lhs => dst = lhs - quotient*rhs
    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;
    out.instrs.push_back(MInstr{MOpcode::MSubRRRR,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, quotient),
                                 MOperand::vregOp(RegClass::GPR, rhs),
                                 MOperand::vregOp(RegClass::GPR, lhs)}});

    return true;
}

//===----------------------------------------------------------------------===//
// Signed Division with Divide-by-Zero Check
//===----------------------------------------------------------------------===//

bool lowerSDivChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out)
{
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::GPR, rhsCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, lhs, lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, rhs, rhsCls))
        return false;

    // Generate divide-by-zero check: cmp rhs, #0; b.eq trap_label
    const std::string trapLabel = ".Ltrap_div0_" + std::to_string(ctx.trapLabelCounter++);
    out.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, rhs), MOperand::immOp(0)}});
    out.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(trapLabel)}});

    // Create trap block
    ctx.mf.blocks.emplace_back();
    ctx.mf.blocks.back().name = trapLabel;
    ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});

    // Compute division: sdiv dst, lhs, rhs
    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;
    out.instrs.push_back(MInstr{MOpcode::SDivRRR,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, lhs),
                                 MOperand::vregOp(RegClass::GPR, rhs)}});

    return true;
}

//===----------------------------------------------------------------------===//
// Unsigned Division with Divide-by-Zero Check
//===----------------------------------------------------------------------===//

bool lowerUDivChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out)
{
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::GPR, rhsCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, lhs, lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, rhs, rhsCls))
        return false;

    // Generate divide-by-zero check: cmp rhs, #0; b.eq trap_label
    const std::string trapLabel = ".Ltrap_div0_" + std::to_string(ctx.trapLabelCounter++);
    out.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, rhs), MOperand::immOp(0)}});
    out.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(trapLabel)}});

    // Create trap block
    ctx.mf.blocks.emplace_back();
    ctx.mf.blocks.back().name = trapLabel;
    ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});

    // Compute division: udiv dst, lhs, rhs
    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;
    out.instrs.push_back(MInstr{MOpcode::UDivRRR,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, lhs),
                                 MOperand::vregOp(RegClass::GPR, rhs)}});

    return true;
}

//===----------------------------------------------------------------------===//
// Unsigned Remainder with Divide-by-Zero Check
//===----------------------------------------------------------------------===//

bool lowerURemChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out)
{
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::GPR, rhsCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, lhs, lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, rhs, rhsCls))
        return false;

    // Generate divide-by-zero check: cmp rhs, #0; b.eq trap_label
    const std::string trapLabel = ".Ltrap_div0_" + std::to_string(ctx.trapLabelCounter++);
    out.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, rhs), MOperand::immOp(0)}});
    out.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(trapLabel)}});

    // Create trap block
    ctx.mf.blocks.emplace_back();
    ctx.mf.blocks.back().name = trapLabel;
    ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});

    // Compute quotient: udiv tmp, lhs, rhs
    const uint16_t quotient = ctx.nextVRegId++;
    out.instrs.push_back(MInstr{MOpcode::UDivRRR,
                                {MOperand::vregOp(RegClass::GPR, quotient),
                                 MOperand::vregOp(RegClass::GPR, lhs),
                                 MOperand::vregOp(RegClass::GPR, rhs)}});

    // Compute remainder: msub dst, quotient, rhs, lhs => dst = lhs - quotient*rhs
    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;
    out.instrs.push_back(MInstr{MOpcode::MSubRRRR,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, quotient),
                                 MOperand::vregOp(RegClass::GPR, rhs),
                                 MOperand::vregOp(RegClass::GPR, lhs)}});

    return true;
}

//===----------------------------------------------------------------------===//
// FP Arithmetic (fadd, fsub, fmul, fdiv)
//===----------------------------------------------------------------------===//

bool lowerFpArithmetic(const il::core::Instr &ins,
                       const il::core::BasicBlock &bb,
                       LoweringContext &ctx,
                       MBasicBlock &out)
{
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::FPR, rhsCls = RegClass::FPR;

    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, lhs, lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, rhs, rhsCls))
        return false;

    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;
    g_tempRegClass[*ins.result] = RegClass::FPR;

    MOpcode mop = MOpcode::FAddRRR;
    switch (ins.op)
    {
        case il::core::Opcode::FAdd:
            mop = MOpcode::FAddRRR;
            break;
        case il::core::Opcode::FSub:
            mop = MOpcode::FSubRRR;
            break;
        case il::core::Opcode::FMul:
            mop = MOpcode::FMulRRR;
            break;
        case il::core::Opcode::FDiv:
            mop = MOpcode::FDivRRR;
            break;
        default:
            return false;
    }

    out.instrs.push_back(MInstr{mop,
                                {MOperand::vregOp(RegClass::FPR, dst),
                                 MOperand::vregOp(RegClass::FPR, lhs),
                                 MOperand::vregOp(RegClass::FPR, rhs)}});
    return true;
}

//===----------------------------------------------------------------------===//
// FP Comparisons
//===----------------------------------------------------------------------===//

bool lowerFpCompare(const il::core::Instr &ins,
                    const il::core::BasicBlock &bb,
                    LoweringContext &ctx,
                    MBasicBlock &out)
{
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::FPR, rhsCls = RegClass::FPR;

    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, lhs, lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, rhs, rhsCls))
        return false;

    // Emit fcmp
    out.instrs.push_back(MInstr{MOpcode::FCmpRR,
                                {MOperand::vregOp(RegClass::FPR, lhs),
                                 MOperand::vregOp(RegClass::FPR, rhs)}});

    // Emit cset with appropriate condition
    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;
    out.instrs.push_back(
        MInstr{MOpcode::Cset,
               {MOperand::vregOp(RegClass::GPR, dst), MOperand::condOp(fpCondCode(ins.op))}});

    return true;
}

//===----------------------------------------------------------------------===//
// sitofp (signed int to float)
//===----------------------------------------------------------------------===//

bool lowerSitofp(const il::core::Instr &ins,
                 const il::core::BasicBlock &bb,
                 LoweringContext &ctx,
                 MBasicBlock &out)
{
    if (!ins.result || ins.operands.empty())
        return false;

    uint16_t sv = 0;
    RegClass scls = RegClass::GPR;
    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, sv, scls))
        return false;

    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;
    g_tempRegClass[*ins.result] = RegClass::FPR;

    out.instrs.push_back(MInstr{MOpcode::SCvtF,
                                {MOperand::vregOp(RegClass::FPR, dst),
                                 MOperand::vregOp(RegClass::GPR, sv)}});
    return true;
}

//===----------------------------------------------------------------------===//
// fptosi (float to signed int)
//===----------------------------------------------------------------------===//

bool lowerFptosi(const il::core::Instr &ins,
                 const il::core::BasicBlock &bb,
                 LoweringContext &ctx,
                 MBasicBlock &out)
{
    if (!ins.result || ins.operands.empty())
        return false;

    uint16_t fv = 0;
    RegClass fcls = RegClass::FPR;
    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, fv, fcls))
        return false;

    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;

    out.instrs.push_back(MInstr{MOpcode::FCvtZS,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::FPR, fv)}});
    return true;
}

//===----------------------------------------------------------------------===//
// Zext1/Trunc1 (Boolean conversion)
//===----------------------------------------------------------------------===//

bool lowerZext1Trunc1(const il::core::Instr &ins,
                      const il::core::BasicBlock &bb,
                      LoweringContext &ctx,
                      MBasicBlock &out)
{
    if (!ins.result || ins.operands.empty())
        return false;

    uint16_t sv = 0;
    RegClass scls = RegClass::GPR;
    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, sv, scls))
        return false;

    const uint16_t dst = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = dst;

    // dst = sv & 1
    const uint16_t one = ctx.nextVRegId++;
    out.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, one), MOperand::immOp(1)}});
    out.instrs.push_back(MInstr{MOpcode::AndRRR,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, sv),
                                 MOperand::vregOp(RegClass::GPR, one)}});
    return true;
}

//===----------------------------------------------------------------------===//
// Narrowing casts (CastSiNarrowChk, CastUiNarrowChk)
//===----------------------------------------------------------------------===//

bool lowerNarrowingCast(const il::core::Instr &ins,
                        const il::core::BasicBlock &bb,
                        LoweringContext &ctx,
                        MBasicBlock &out)
{
    if (!ins.result || ins.operands.empty())
        return false;

    int bits = 64;
    if (ins.type.kind == il::core::Type::Kind::I16)
        bits = 16;
    else if (ins.type.kind == il::core::Type::Kind::I32)
        bits = 32;
    const int sh = 64 - bits;

    uint16_t sv = 0;
    RegClass scls = RegClass::GPR;
    if (!materializeValueToVReg(ins.operands[0], bb, ctx.ti, ctx.fb, out, ctx.tempVReg,
                                ctx.nextVRegId, sv, scls))
        return false;

    const uint16_t vt = ctx.nextVRegId++;
    ctx.tempVReg[*ins.result] = vt;

    // vt = narrowed version of sv
    if (sh > 0)
    {
        // Copy sv into vt first
        out.instrs.push_back(MInstr{MOpcode::MovRR,
                                    {MOperand::vregOp(RegClass::GPR, vt),
                                     MOperand::vregOp(RegClass::GPR, sv)}});
        if (ins.op == il::core::Opcode::CastSiNarrowChk)
        {
            out.instrs.push_back(MInstr{MOpcode::LslRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
            out.instrs.push_back(MInstr{MOpcode::AsrRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
        }
        else
        {
            out.instrs.push_back(MInstr{MOpcode::LslRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
            out.instrs.push_back(MInstr{MOpcode::LsrRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
        }
    }
    else
    {
        // No change in width - just copy
        out.instrs.push_back(MInstr{MOpcode::MovRR,
                                    {MOperand::vregOp(RegClass::GPR, vt),
                                     MOperand::vregOp(RegClass::GPR, sv)}});
    }
    return true;
}

} // namespace viper::codegen::aarch64
