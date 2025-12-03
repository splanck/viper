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
                      MBasicBlock &bbOutRef)
{
    switch (ins.op)
    {
        case Opcode::Zext1:
        case Opcode::Trunc1:
        {
            if (!ins.result || ins.operands.empty())
                return true; // Handled (as no-op for invalid input)
            uint16_t sv = 0;
            RegClass scls = RegClass::GPR;
            if (!materializeValueToVReg(ins.operands[0], bbIn, ctx.ti, ctx.fb, bbOutRef,
                                        ctx.tempVReg, ctx.nextVRegId, sv, scls))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            const uint16_t one = ctx.nextVRegId++;
            bbOutRef.instrs.push_back(
                MInstr{MOpcode::MovRI,
                       {MOperand::vregOp(RegClass::GPR, one), MOperand::immOp(1)}});
            bbOutRef.instrs.push_back(MInstr{MOpcode::AndRRR,
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
            if (!materializeValueToVReg(ins.operands[0], bbIn, ctx.ti, ctx.fb, bbOutRef,
                                        ctx.tempVReg, ctx.nextVRegId, sv, scls))
                return true;
            const uint16_t vt = ctx.nextVRegId++;
            if (sh > 0)
            {
                bbOutRef.instrs.push_back(MInstr{MOpcode::MovRR,
                                                 {MOperand::vregOp(RegClass::GPR, vt),
                                                  MOperand::vregOp(RegClass::GPR, sv)}});
                if (ins.op == Opcode::CastSiNarrowChk)
                {
                    bbOutRef.instrs.push_back(MInstr{MOpcode::LslRI,
                                                     {MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::immOp(sh)}});
                    bbOutRef.instrs.push_back(MInstr{MOpcode::AsrRI,
                                                     {MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::immOp(sh)}});
                }
                else
                {
                    bbOutRef.instrs.push_back(MInstr{MOpcode::LslRI,
                                                     {MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::immOp(sh)}});
                    bbOutRef.instrs.push_back(MInstr{MOpcode::LsrRI,
                                                     {MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::vregOp(RegClass::GPR, vt),
                                                      MOperand::immOp(sh)}});
                }
            }
            else
            {
                bbOutRef.instrs.push_back(MInstr{MOpcode::MovRR,
                                                 {MOperand::vregOp(RegClass::GPR, vt),
                                                  MOperand::vregOp(RegClass::GPR, sv)}});
            }
            bbOutRef.instrs.push_back(MInstr{MOpcode::CmpRR,
                                             {MOperand::vregOp(RegClass::GPR, vt),
                                              MOperand::vregOp(RegClass::GPR, sv)}});
            const std::string trapLabel = ".Ltrap_cast_" + std::to_string(ctx.trapLabelCounter++);
            bbOutRef.instrs.push_back(
                MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel)}});
            ctx.mf.blocks.emplace_back();
            ctx.mf.blocks.back().name = trapLabel;
            ctx.mf.blocks.back().instrs.push_back(
                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            bbOutRef.instrs.push_back(MInstr{MOpcode::MovRR,
                                             {MOperand::vregOp(RegClass::GPR, dst),
                                              MOperand::vregOp(RegClass::GPR, vt)}});
            return true;
        }
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        {
            if (!ins.result || ins.operands.empty())
                return true;
            uint16_t fv = 0;
            RegClass fcls = RegClass::FPR;
            if (!materializeValueToVReg(ins.operands[0], bbIn, ctx.ti, ctx.fb, bbOutRef,
                                        ctx.tempVReg, ctx.nextVRegId, fv, fcls))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            if (ins.op == Opcode::CastFpToSiRteChk)
            {
                bbOutRef.instrs.push_back(MInstr{MOpcode::FCvtZS,
                                                 {MOperand::vregOp(RegClass::GPR, dst),
                                                  MOperand::vregOp(RegClass::FPR, fv)}});
            }
            else
            {
                bbOutRef.instrs.push_back(MInstr{MOpcode::FCvtZU,
                                                 {MOperand::vregOp(RegClass::GPR, dst),
                                                  MOperand::vregOp(RegClass::FPR, fv)}});
            }
            const uint16_t rr = ctx.nextVRegId++;
            if (ins.op == Opcode::CastFpToSiRteChk)
            {
                bbOutRef.instrs.push_back(MInstr{MOpcode::SCvtF,
                                                 {MOperand::vregOp(RegClass::FPR, rr),
                                                  MOperand::vregOp(RegClass::GPR, dst)}});
            }
            else
            {
                bbOutRef.instrs.push_back(MInstr{MOpcode::UCvtF,
                                                 {MOperand::vregOp(RegClass::FPR, rr),
                                                  MOperand::vregOp(RegClass::GPR, dst)}});
            }
            bbOutRef.instrs.push_back(MInstr{MOpcode::FCmpRR,
                                             {MOperand::vregOp(RegClass::FPR, fv),
                                              MOperand::vregOp(RegClass::FPR, rr)}});
            {
                const std::string trapLabel2 =
                    ".Ltrap_fpcast_" + std::to_string(ctx.trapLabelCounter++);
                bbOutRef.instrs.push_back(
                    MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel2)}});
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
            if (!materializeValueToVReg(ins.operands[0], bbIn, ctx.ti, ctx.fb, bbOutRef,
                                        ctx.tempVReg, ctx.nextVRegId, sv, scls))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            if (ins.op == Opcode::CastSiToFp)
                bbOutRef.instrs.push_back(MInstr{MOpcode::SCvtF,
                                                 {MOperand::vregOp(RegClass::FPR, dst),
                                                  MOperand::vregOp(RegClass::GPR, sv)}});
            else
                bbOutRef.instrs.push_back(MInstr{MOpcode::UCvtF,
                                                 {MOperand::vregOp(RegClass::FPR, dst),
                                                  MOperand::vregOp(RegClass::GPR, sv)}});
            return true;
        }
        case Opcode::SRemChk0:
            lowerSRemChk0(ins, bbIn, ctx, bbOutRef);
            return true;
        case Opcode::SDivChk0:
            lowerSDivChk0(ins, bbIn, ctx, bbOutRef);
            return true;
        case Opcode::UDivChk0:
            lowerUDivChk0(ins, bbIn, ctx, bbOutRef);
            return true;
        case Opcode::URemChk0:
            lowerURemChk0(ins, bbIn, ctx, bbOutRef);
            return true;
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            lowerFpArithmetic(ins, bbIn, ctx, bbOutRef);
            return true;
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            lowerFpCompare(ins, bbIn, ctx, bbOutRef);
            return true;
        case Opcode::Sitofp:
            lowerSitofp(ins, bbIn, ctx, bbOutRef);
            return true;
        case Opcode::Fptosi:
            lowerFptosi(ins, bbIn, ctx, bbOutRef);
            return true;
        default:
            // Opcode not handled - caller should process
            return false;
    }
}

} // namespace viper::codegen::aarch64
