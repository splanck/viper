//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/OpcodeDispatch.cpp
// Purpose: Instruction lowering dispatch for IL->MIR conversion.
//
// This file contains the main instruction lowering switch statement,
// extracted from LowerILToMIR.cpp to reduce function size.
//
//===----------------------------------------------------------------------===//

#include "OpcodeDispatch.hpp"
#include "InstrLowering.hpp"
#include "OpcodeMappings.hpp"

namespace viper::codegen::aarch64
{

using il::core::Opcode;

static const char *condForOpcode(Opcode op)
{
    return lookupCondition(op);
}

bool lowerInstruction(const il::core::Instr &ins,
                      const il::core::BasicBlock &bbIn,
                      LoweringContext &ctx,
                      std::size_t bbOutIdx)
{
    // Helper lambda to access the output block by index.
    // This ensures we always get a valid reference even after emplace_back().
    auto bbOut = [&]() -> MBasicBlock & { return ctx.mf.blocks[bbOutIdx]; };

    switch (ins.op)
    {
        case Opcode::Zext1:
        case Opcode::Trunc1:
        {
            if (!ins.result || ins.operands.empty())
                return true; // Handled (as no-op for invalid input)
            uint16_t sv = 0;
            RegClass scls = RegClass::GPR;
            if (!materializeValueToVReg(ins.operands[0],
                                        bbIn,
                                        ctx.ti,
                                        ctx.fb,
                                        bbOut(),
                                        ctx.tempVReg,
                                        ctx.nextVRegId,
                                        sv,
                                        scls))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            const uint16_t one = ctx.nextVRegId++;
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, one), MOperand::immOp(1)}});
            bbOut().instrs.push_back(MInstr{MOpcode::AndRRR,
                                            {MOperand::vregOp(RegClass::GPR, dst),
                                             MOperand::vregOp(RegClass::GPR, sv),
                                             MOperand::vregOp(RegClass::GPR, one)}});
            return true;
        }
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
        {
            if (!ins.result || ins.operands.empty())
                return true;
            int bits = 64;
            if (ins.type.kind == il::core::Type::Kind::I16)
                bits = 16;
            else if (ins.type.kind == il::core::Type::Kind::I32)
                bits = 32;
            const int sh = 64 - bits;
            uint16_t sv = 0;
            RegClass scls = RegClass::GPR;
            if (!materializeValueToVReg(ins.operands[0],
                                        bbIn,
                                        ctx.ti,
                                        ctx.fb,
                                        bbOut(),
                                        ctx.tempVReg,
                                        ctx.nextVRegId,
                                        sv,
                                        scls))
                return true;
            const uint16_t vt = ctx.nextVRegId++;
            if (sh > 0)
            {
                bbOut().instrs.push_back(MInstr{
                    MOpcode::MovRR,
                    {MOperand::vregOp(RegClass::GPR, vt), MOperand::vregOp(RegClass::GPR, sv)}});
                if (ins.op == Opcode::CastSiNarrowChk)
                {
                    bbOut().instrs.push_back(MInstr{MOpcode::LslRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                    bbOut().instrs.push_back(MInstr{MOpcode::AsrRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                }
                else
                {
                    bbOut().instrs.push_back(MInstr{MOpcode::LslRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                    bbOut().instrs.push_back(MInstr{MOpcode::LsrRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                }
            }
            else
            {
                bbOut().instrs.push_back(MInstr{
                    MOpcode::MovRR,
                    {MOperand::vregOp(RegClass::GPR, vt), MOperand::vregOp(RegClass::GPR, sv)}});
            }
            bbOut().instrs.push_back(
                MInstr{MOpcode::CmpRR,
                       {MOperand::vregOp(RegClass::GPR, vt), MOperand::vregOp(RegClass::GPR, sv)}});
            const std::string trapLabel = ".Ltrap_cast_" + std::to_string(ctx.trapLabelCounter++);
            bbOut().instrs.push_back(
                MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel)}});
            ctx.mf.blocks.emplace_back();
            ctx.mf.blocks.back().name = trapLabel;
            ctx.mf.blocks.back().instrs.push_back(
                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            bbOut().instrs.push_back(MInstr{
                MOpcode::MovRR,
                {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::GPR, vt)}});
            return true;
        }
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        {
            if (!ins.result || ins.operands.empty())
                return true;
            uint16_t fv = 0;
            RegClass fcls = RegClass::FPR;
            if (!materializeValueToVReg(ins.operands[0],
                                        bbIn,
                                        ctx.ti,
                                        ctx.fb,
                                        bbOut(),
                                        ctx.tempVReg,
                                        ctx.nextVRegId,
                                        fv,
                                        fcls))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            if (ins.op == Opcode::CastFpToSiRteChk)
            {
                bbOut().instrs.push_back(MInstr{
                    MOpcode::FCvtZS,
                    {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::FPR, fv)}});
            }
            else
            {
                bbOut().instrs.push_back(MInstr{
                    MOpcode::FCvtZU,
                    {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::FPR, fv)}});
            }
            const uint16_t rr = ctx.nextVRegId++;
            if (ins.op == Opcode::CastFpToSiRteChk)
            {
                bbOut().instrs.push_back(MInstr{
                    MOpcode::SCvtF,
                    {MOperand::vregOp(RegClass::FPR, rr), MOperand::vregOp(RegClass::GPR, dst)}});
            }
            else
            {
                bbOut().instrs.push_back(MInstr{
                    MOpcode::UCvtF,
                    {MOperand::vregOp(RegClass::FPR, rr), MOperand::vregOp(RegClass::GPR, dst)}});
            }
            bbOut().instrs.push_back(
                MInstr{MOpcode::FCmpRR,
                       {MOperand::vregOp(RegClass::FPR, fv), MOperand::vregOp(RegClass::FPR, rr)}});
            {
                const std::string trapLabel2 =
                    ".Ltrap_fpcast_" + std::to_string(ctx.trapLabelCounter++);
                bbOut().instrs.push_back(MInstr{
                    MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel2)}});
                ctx.mf.blocks.emplace_back();
                ctx.mf.blocks.back().name = trapLabel2;
                ctx.mf.blocks.back().instrs.push_back(
                    MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
            }
            return true;
        }
        case Opcode::CastSiToFp:
        case Opcode::CastUiToFp:
        {
            if (!ins.result || ins.operands.empty())
                return true;
            uint16_t sv = 0;
            RegClass scls = RegClass::GPR;
            if (!materializeValueToVReg(ins.operands[0],
                                        bbIn,
                                        ctx.ti,
                                        ctx.fb,
                                        bbOut(),
                                        ctx.tempVReg,
                                        ctx.nextVRegId,
                                        sv,
                                        scls))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            g_tempRegClass[*ins.result] = RegClass::FPR;
            if (ins.op == Opcode::CastSiToFp)
                bbOut().instrs.push_back(MInstr{
                    MOpcode::SCvtF,
                    {MOperand::vregOp(RegClass::FPR, dst), MOperand::vregOp(RegClass::GPR, sv)}});
            else
                bbOut().instrs.push_back(MInstr{
                    MOpcode::UCvtF,
                    {MOperand::vregOp(RegClass::FPR, dst), MOperand::vregOp(RegClass::GPR, sv)}});
            return true;
        }
        case Opcode::SRemChk0:
            lowerSRemChk0(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::SDivChk0:
            lowerSDivChk0(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::UDivChk0:
            lowerUDivChk0(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::URemChk0:
            lowerURemChk0(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            lowerFpArithmetic(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            lowerFpCompare(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Sitofp:
            lowerSitofp(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Fptosi:
            lowerFptosi(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::ConstStr:
        {
            // Lower const_str to produce a string handle via rt_const_cstr.
            // This must be lowered proactively (not demand-lowered) when the result
            // is a cross-block temp that will be spilled.
            if (!ins.result || ins.operands.empty())
                return true;
            if (ins.operands[0].kind != il::core::Value::Kind::GlobalAddr)
                return true;
            const std::string &sym = ins.operands[0].str;
            // Materialize address of pooled literal label
            const uint16_t litPtrV = ctx.nextVRegId++;
            bbOut().instrs.push_back(
                MInstr{MOpcode::AdrPage,
                       {MOperand::vregOp(RegClass::GPR, litPtrV), MOperand::labelOp(sym)}});
            bbOut().instrs.push_back(MInstr{MOpcode::AddPageOff,
                                            {MOperand::vregOp(RegClass::GPR, litPtrV),
                                             MOperand::vregOp(RegClass::GPR, litPtrV),
                                             MOperand::labelOp(sym)}});
            // Call rt_const_cstr(litPtr) to obtain an rt_string handle in x0
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRR,
                       {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, litPtrV)}});
            bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_const_cstr")}});
            // Move x0 (rt_string) into a fresh vreg as the const_str result
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRR,
                       {MOperand::vregOp(RegClass::GPR, dst), MOperand::regOp(PhysReg::X0)}});
            return true;
        }
        case Opcode::Store:
        {
            if (ins.operands.size() != 2)
                return true;
            if (ins.operands[0].kind != il::core::Value::Kind::Temp)
                return true;
            const unsigned ptrId = ins.operands[0].id;
            const int off = ctx.fb.localOffset(ptrId);
            if (off != 0)
            {
                // Store to alloca local via FP offset
                uint16_t v = 0;
                RegClass cls = RegClass::GPR;
                if (materializeValueToVReg(ins.operands[1],
                                           bbIn,
                                           ctx.ti,
                                           ctx.fb,
                                           bbOut(),
                                           ctx.tempVReg,
                                           ctx.nextVRegId,
                                           v,
                                           cls))
                {
                    const bool dstIsFP = (ins.type.kind == il::core::Type::Kind::F64);
                    if (dstIsFP)
                    {
                        uint16_t srcF = v;
                        if (cls != RegClass::FPR)
                        {
                            srcF = ctx.nextVRegId++;
                            bbOut().instrs.push_back(MInstr{MOpcode::SCvtF,
                                                            {MOperand::vregOp(RegClass::FPR, srcF),
                                                             MOperand::vregOp(RegClass::GPR, v)}});
                        }
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::StrFprFpImm,
                                   {MOperand::vregOp(RegClass::FPR, srcF), MOperand::immOp(off)}});
                    }
                    else
                    {
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::StrRegFpImm,
                                   {MOperand::vregOp(RegClass::GPR, v), MOperand::immOp(off)}});
                    }
                }
            }
            else
            {
                // General store via base-in-vreg
                uint16_t vbase = 0, vval = 0;
                RegClass cbase = RegClass::GPR, cval = RegClass::GPR;
                if (materializeValueToVReg(ins.operands[0],
                                           bbIn,
                                           ctx.ti,
                                           ctx.fb,
                                           bbOut(),
                                           ctx.tempVReg,
                                           ctx.nextVRegId,
                                           vbase,
                                           cbase) &&
                    materializeValueToVReg(ins.operands[1],
                                           bbIn,
                                           ctx.ti,
                                           ctx.fb,
                                           bbOut(),
                                           ctx.tempVReg,
                                           ctx.nextVRegId,
                                           vval,
                                           cval))
                {
                    const bool dstIsFP = (ins.type.kind == il::core::Type::Kind::F64);
                    if (dstIsFP)
                    {
                        // Float store: ensure value is in FPR, then use StrFprBaseImm
                        uint16_t srcF = vval;
                        if (cval != RegClass::FPR)
                        {
                            srcF = ctx.nextVRegId++;
                            bbOut().instrs.push_back(
                                MInstr{MOpcode::SCvtF,
                                       {MOperand::vregOp(RegClass::FPR, srcF),
                                        MOperand::vregOp(RegClass::GPR, vval)}});
                        }
                        bbOut().instrs.push_back(MInstr{MOpcode::StrFprBaseImm,
                                                        {MOperand::vregOp(RegClass::FPR, srcF),
                                                         MOperand::vregOp(RegClass::GPR, vbase),
                                                         MOperand::immOp(0)}});
                    }
                    else
                    {
                        bbOut().instrs.push_back(MInstr{MOpcode::StrRegBaseImm,
                                                        {MOperand::vregOp(RegClass::GPR, vval),
                                                         MOperand::vregOp(RegClass::GPR, vbase),
                                                         MOperand::immOp(0)}});
                    }
                }
            }
            return true;
        }
        case Opcode::GEP:
        {
            // GEP computes base + offset and produces a pointer result.
            if (!ins.result || ins.operands.size() < 2)
                return true;
            uint16_t vbase = 0;
            RegClass cbase = RegClass::GPR;
            if (!materializeValueToVReg(ins.operands[0],
                                        bbIn,
                                        ctx.ti,
                                        ctx.fb,
                                        bbOut(),
                                        ctx.tempVReg,
                                        ctx.nextVRegId,
                                        vbase,
                                        cbase))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            const auto &offVal = ins.operands[1];
            if (offVal.kind == il::core::Value::Kind::ConstInt)
            {
                const long long imm = offVal.i64;
                if (imm == 0)
                {
                    bbOut().instrs.push_back(MInstr{MOpcode::MovRR,
                                                    {MOperand::vregOp(RegClass::GPR, dst),
                                                     MOperand::vregOp(RegClass::GPR, vbase)}});
                }
                else
                {
                    bbOut().instrs.push_back(MInstr{MOpcode::AddRI,
                                                    {MOperand::vregOp(RegClass::GPR, dst),
                                                     MOperand::vregOp(RegClass::GPR, vbase),
                                                     MOperand::immOp(imm)}});
                }
            }
            else
            {
                uint16_t voff = 0;
                RegClass coff = RegClass::GPR;
                if (materializeValueToVReg(offVal,
                                           bbIn,
                                           ctx.ti,
                                           ctx.fb,
                                           bbOut(),
                                           ctx.tempVReg,
                                           ctx.nextVRegId,
                                           voff,
                                           coff))
                {
                    bbOut().instrs.push_back(MInstr{MOpcode::AddRRR,
                                                    {MOperand::vregOp(RegClass::GPR, dst),
                                                     MOperand::vregOp(RegClass::GPR, vbase),
                                                     MOperand::vregOp(RegClass::GPR, voff)}});
                }
            }
            return true;
        }
        case Opcode::Load:
        {
            if (!ins.result || ins.operands.empty())
                return true;
            if (ins.operands[0].kind != il::core::Value::Kind::Temp)
                return true;
            const unsigned ptrId = ins.operands[0].id;
            const int off = ctx.fb.localOffset(ptrId);
            if (off != 0)
            {
                // Load from alloca local via FP offset
                const bool isFP = (ins.type.kind == il::core::Type::Kind::F64);
                const uint16_t dst = ctx.nextVRegId++;
                ctx.tempVReg[*ins.result] = dst;
                if (isFP)
                {
                    ctx.tempRegClass[*ins.result] = RegClass::FPR;
                    bbOut().instrs.push_back(
                        MInstr{MOpcode::LdrFprFpImm,
                               {MOperand::vregOp(RegClass::FPR, dst), MOperand::immOp(off)}});
                }
                else
                {
                    bbOut().instrs.push_back(
                        MInstr{MOpcode::LdrRegFpImm,
                               {MOperand::vregOp(RegClass::GPR, dst), MOperand::immOp(off)}});
                }
            }
            else
            {
                // General load via base-in-vreg
                uint16_t vbase = 0;
                RegClass cbase = RegClass::GPR;
                if (materializeValueToVReg(ins.operands[0],
                                           bbIn,
                                           ctx.ti,
                                           ctx.fb,
                                           bbOut(),
                                           ctx.tempVReg,
                                           ctx.nextVRegId,
                                           vbase,
                                           cbase))
                {
                    const bool isFP = (ins.type.kind == il::core::Type::Kind::F64);
                    const uint16_t dst = ctx.nextVRegId++;
                    ctx.tempVReg[*ins.result] = dst;
                    if (isFP)
                    {
                        ctx.tempRegClass[*ins.result] = RegClass::FPR;
                        bbOut().instrs.push_back(MInstr{MOpcode::LdrFprBaseImm,
                                                        {MOperand::vregOp(RegClass::FPR, dst),
                                                         MOperand::vregOp(RegClass::GPR, vbase),
                                                         MOperand::immOp(0)}});
                    }
                    else
                    {
                        bbOut().instrs.push_back(MInstr{MOpcode::LdrRegBaseImm,
                                                        {MOperand::vregOp(RegClass::GPR, dst),
                                                         MOperand::vregOp(RegClass::GPR, vbase),
                                                         MOperand::immOp(0)}});
                    }
                }
            }
            return true;
        }
        case Opcode::Call:
        {
            LoweredCall seq{};
            if (lowerCallWithArgs(
                    ins, bbIn, ctx.ti, ctx.fb, bbOut(), seq, ctx.tempVReg, ctx.nextVRegId))
            {
                for (auto &mi : seq.prefix)
                    bbOut().instrs.push_back(std::move(mi));
                bbOut().instrs.push_back(std::move(seq.call));
                for (auto &mi : seq.postfix)
                    bbOut().instrs.push_back(std::move(mi));
                // If the call produces a result, move x0/v0 to a fresh vreg
                if (ins.result)
                {
                    const uint16_t dst = ctx.nextVRegId++;
                    ctx.tempVReg[*ins.result] = dst;
                    if (ins.type.kind == il::core::Type::Kind::F64)
                    {
                        ctx.tempRegClass[*ins.result] = RegClass::FPR;
                        bbOut().instrs.push_back(MInstr{MOpcode::FMovRR,
                                                        {MOperand::vregOp(RegClass::FPR, dst),
                                                         MOperand::regOp(ctx.ti.f64ReturnReg)}});
                    }
                    else
                    {
                        bbOut().instrs.push_back(MInstr{
                            MOpcode::MovRR,
                            {MOperand::vregOp(RegClass::GPR, dst), MOperand::regOp(PhysReg::X0)}});
                    }
                    // Special handling for rt_arr_obj_get - spill and reload
                    if (ins.callee == "rt_arr_obj_get")
                    {
                        const int off = ctx.fb.ensureSpill(dst);
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::StrRegFpImm,
                                   {MOperand::vregOp(RegClass::GPR, dst), MOperand::immOp(off)}});
                        const uint16_t dst2 = ctx.nextVRegId++;
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::LdrRegFpImm,
                                   {MOperand::vregOp(RegClass::GPR, dst2), MOperand::immOp(off)}});
                        ctx.tempVReg[*ins.result] = dst2;
                    }
                }
            }
            else if (!ins.callee.empty())
            {
                // Fallback: emit call without args for noreturn functions
                bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp(ins.callee)}});
            }
            return true;
        }
        case Opcode::Ret:
        {
            if (!ins.operands.empty())
            {
                uint16_t v = 0;
                RegClass cls = RegClass::GPR;
                bool ok = materializeValueToVReg(ins.operands[0],
                                                 bbIn,
                                                 ctx.ti,
                                                 ctx.fb,
                                                 bbOut(),
                                                 ctx.tempVReg,
                                                 ctx.nextVRegId,
                                                 v,
                                                 cls);
                // Special-case: const_str producer when generic materialization fails
                if (!ok && ins.operands[0].kind == il::core::Value::Kind::Temp)
                {
                    const unsigned rid = ins.operands[0].id;
                    auto it = std::find_if(bbIn.instructions.begin(),
                                           bbIn.instructions.end(),
                                           [&](const il::core::Instr &I)
                                           { return I.result && *I.result == rid; });
                    if (it != bbIn.instructions.end())
                    {
                        const auto &prod = *it;
                        if ((prod.op == Opcode::ConstStr || prod.op == Opcode::AddrOf) &&
                            !prod.operands.empty() &&
                            prod.operands[0].kind == il::core::Value::Kind::GlobalAddr)
                        {
                            v = ctx.nextVRegId++;
                            cls = RegClass::GPR;
                            const std::string &sym = prod.operands[0].str;
                            bbOut().instrs.push_back(MInstr{
                                MOpcode::AdrPage,
                                {MOperand::vregOp(RegClass::GPR, v), MOperand::labelOp(sym)}});
                            bbOut().instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                            {MOperand::vregOp(RegClass::GPR, v),
                                                             MOperand::vregOp(RegClass::GPR, v),
                                                             MOperand::labelOp(sym)}});
                            ctx.tempVReg[rid] = v;
                            ok = true;
                        }
                    }
                }
                if (ok)
                {
                    if (cls == RegClass::FPR)
                    {
                        bbOut().instrs.push_back(MInstr{MOpcode::FMovRR,
                                                        {MOperand::regOp(ctx.ti.f64ReturnReg),
                                                         MOperand::vregOp(RegClass::FPR, v)}});
                    }
                    else
                    {
                        bbOut().instrs.push_back(MInstr{
                            MOpcode::MovRR,
                            {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, v)}});
                    }
                }
            }
            bbOut().instrs.push_back(MInstr{MOpcode::Ret, {}});
            return true;
        }
        case Opcode::Alloca:
            // Alloca is handled during frame building, no MIR needed here
            return true;
        case Opcode::Br:
        case Opcode::CBr:
            // Terminators are lowered in a separate pass after all instructions
            return true;
        default:
            // Opcode not handled - caller should process
            return false;
    }
}

} // namespace viper::codegen::aarch64
