//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file OpcodeDispatch.cpp
/// @brief Instruction opcode dispatch for IL to MIR lowering on AArch64.
///
/// This file implements the main instruction lowering switch statement that
/// dispatches IL opcodes to their appropriate MIR lowering handlers. It serves
/// as the central routing point for converting individual IL instructions into
/// sequences of AArch64 machine instructions.
///
/// **Dispatch Architecture:**
/// ```
/// ┌─────────────────────────────────────────────────────────────────────────┐
/// │                    lowerInstruction() Entry Point                       │
/// └────────────────────────────────────┬────────────────────────────────────┘
///                                      │
///           ┌──────────────────────────┼──────────────────────────────────┐
///           │                          │                                  │
///           ▼                          ▼                                  ▼
/// ┌─────────────────┐      ┌─────────────────┐            ┌─────────────────┐
/// │  Type Casts     │      │  Arithmetic     │            │  Memory/Call    │
/// │  Zext1, Trunc1  │      │  FAdd, FSub,    │            │  Store, Load,   │
/// │  CastSiNarrowChk│      │  FMul, FDiv,    │            │  GEP, Call,     │
/// │  CastFpToSiRte  │      │  SDivChk0, etc  │            │  Ret, Alloca    │
/// └─────────────────┘      └─────────────────┘            └─────────────────┘
///           │                          │                                  │
///           └──────────────────────────┼──────────────────────────────────┘
///                                      ▼
///                         ┌─────────────────────┐
///                         │  MIR Instructions   │
///                         │  added to bbOut()   │
///                         └─────────────────────┘
/// ```
///
/// **Opcode Categories Handled:**
///
/// | Category          | Opcodes                                           |
/// |-------------------|---------------------------------------------------|
/// | Bit Manipulation  | Zext1, Trunc1                                     |
/// | Integer Casts     | CastSiNarrowChk, CastUiNarrowChk                  |
/// | FP Casts          | CastFpToSiRteChk, CastFpToUiRteChk                |
/// | Int-to-FP         | CastSiToFp, CastUiToFp, Sitofp                    |
/// | FP-to-Int         | Fptosi                                            |
/// | Checked Division  | SRemChk0, SDivChk0, UDivChk0, URemChk0            |
/// | FP Arithmetic     | FAdd, FSub, FMul, FDiv                            |
/// | FP Comparison     | FCmpEQ, FCmpNE, FCmpLT, FCmpLE, FCmpGT, FCmpGE    |
/// | Memory Ops        | Store, Load, GEP, Alloca                          |
/// | Control Flow      | Call, Ret, Br, CBr (terminators deferred)         |
/// | Constants         | ConstStr                                          |
///
/// **Value Materialization:**
/// Each handler uses `materializeValueToVReg()` from InstrLowering.hpp to
/// convert IL values (temps, constants, globals) into virtual registers:
/// ```cpp
/// uint16_t vreg = 0;
/// RegClass cls = RegClass::GPR;
/// if (materializeValueToVReg(operand, bbIn, ti, fb, bbOut(),
///                            tempVReg, tempRegClass, nextVRegId, vreg, cls))
/// {
///     // Use vreg in MIR instruction
/// }
/// ```
///
/// **Trap Blocks for Checked Operations:**
/// Checked operations (CastSiNarrowChk, SDivChk0, etc.) generate trap blocks
/// that branch to `rt_trap` on overflow or divide-by-zero:
/// ```
/// Block:                    Trap Block:
/// cmp original, widened     .Ltrap_cast_N:
/// b.ne .Ltrap_cast_N    →     bl rt_trap
/// mov result, value
/// ```
///
/// **Return Value Convention:**
/// - Returns `true` if the opcode was handled (MIR emitted)
/// - Returns `false` if the opcode should be handled by the caller
/// - Terminators (Br, CBr) return `true` but are lowered in a separate pass
///
/// **Cross-Block Value Handling:**
/// The dispatch operates within the context of `LoweringContext` which tracks:
/// - `tempVReg`: Map from IL temp ID to MIR vreg ID
/// - `tempRegClass`: Register class (GPR/FPR) for each temp
/// - `crossBlockSpillOffset`: Spill slots for cross-block live values
///
/// @see InstrLowering.cpp For individual opcode lowering implementations
/// @see LowerILToMIR.cpp For the main lowering orchestrator
/// @see TerminatorLowering.cpp For control-flow terminator handling
///
//===----------------------------------------------------------------------===//

#include "OpcodeDispatch.hpp"
#include "InstrLowering.hpp"
#include "OpcodeMappings.hpp"

#include "il/core/Opcode.hpp"

#include <cstdio>
#include <cstring>

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
                                        ctx.tempRegClass,
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
                                        ctx.tempRegClass,
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
            // Per IL spec: .rte = round-to-even, .chk = trap on overflow only.
            // First round to nearest even, then convert to integer.
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
                                        ctx.tempRegClass,
                                        ctx.nextVRegId,
                                        fv,
                                        fcls))
                return true;

            // Round to nearest even first (frintn)
            const uint16_t rounded = ctx.nextVRegId++;
            bbOut().instrs.push_back(MInstr{
                MOpcode::FRintN,
                {MOperand::vregOp(RegClass::FPR, rounded), MOperand::vregOp(RegClass::FPR, fv)}});

            // Convert rounded value to integer
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            if (ins.op == Opcode::CastFpToSiRteChk)
            {
                bbOut().instrs.push_back(MInstr{MOpcode::FCvtZS,
                                                {MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::vregOp(RegClass::FPR, rounded)}});
            }
            else
            {
                bbOut().instrs.push_back(MInstr{MOpcode::FCvtZU,
                                                {MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::vregOp(RegClass::FPR, rounded)}});
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
                                        ctx.tempRegClass,
                                        ctx.nextVRegId,
                                        sv,
                                        scls))
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::FPR;
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
        case Opcode::IdxChk:
            lowerIdxChk(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::SRem:
            lowerSRem(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::URem:
            lowerURem(ins, bbIn, ctx, bbOut());
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
        case Opcode::FCmpOrd:
        case Opcode::FCmpUno:
            lowerFpCompare(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Sitofp:
            lowerSitofp(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Fptosi:
            lowerFptosi(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::ConstF64:
        {
            // Lower const.f64 by materializing a floating-point constant.
            // For arbitrary values, we load the 64-bit IEEE-754 representation
            // into a GPR and then use fmov to transfer to FPR.
            if (!ins.result || ins.operands.empty())
                return true;

            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::FPR;

            // Get the f64 value from the operand
            if (ins.operands[0].kind == il::core::Value::Kind::ConstFloat)
            {
                // Materialize the 64-bit representation into a GPR, then fmov to FPR
                const double fval = ins.operands[0].f64;
                uint64_t bits;
                std::memcpy(&bits, &fval, sizeof(bits));

                const uint16_t tmpGpr = ctx.nextVRegId++;
                // Use MovRI - the emitter will handle movz/movk sequence automatically
                bbOut().instrs.push_back(MInstr{MOpcode::MovRI,
                                                {MOperand::vregOp(RegClass::GPR, tmpGpr),
                                                 MOperand::immOp(static_cast<long long>(bits))}});
                // Use fmov to transfer GPR to FPR
                bbOut().instrs.push_back(MInstr{MOpcode::FMovGR,
                                                {MOperand::vregOp(RegClass::FPR, dst),
                                                 MOperand::vregOp(RegClass::GPR, tmpGpr)}});
            }
            return true;
        }
        case Opcode::ConstNull:
        {
            // const_null produces a null pointer (0)
            if (!ins.result)
                return true;
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::GPR;
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, dst), MOperand::immOp(0)}});
            return true;
        }
        case Opcode::GAddr:
        {
            // gaddr @symbol produces the address of a global variable
            if (!ins.result || ins.operands.empty())
                return true;
            if (ins.operands[0].kind != il::core::Value::Kind::GlobalAddr)
                return true;
            const std::string &sym = ins.operands[0].str;
            // Materialize address of global symbol using adrp+add
            const uint16_t dst = ctx.nextVRegId++;
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::GPR;
            bbOut().instrs.push_back(MInstr{
                MOpcode::AdrPage, {MOperand::vregOp(RegClass::GPR, dst), MOperand::labelOp(sym)}});
            bbOut().instrs.push_back(MInstr{MOpcode::AddPageOff,
                                            {MOperand::vregOp(RegClass::GPR, dst),
                                             MOperand::vregOp(RegClass::GPR, dst),
                                             MOperand::labelOp(sym)}});
            return true;
        }
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
            const bool isStr = (ins.type.kind == il::core::Type::Kind::Str);
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
                                           ctx.tempRegClass,
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
                    else if (isStr)
                    {
                        // String store to alloca: retain the new value before storing.
                        // Without this retain, consuming runtime functions like
                        // rt_str_concat (which unrefs both inputs) can drop the
                        // refcount to zero prematurely, causing use-after-free.
                        // The VM's storeSlotToPtr always does retain+release for
                        // Str stores.  We omit the release of the old value here
                        // because native alloca slots are not zero-initialised and
                        // the first store would pass garbage to rt_str_release_maybe.
                        // This matches the VM's effective behaviour: the VM retains
                        // on every alloca store but never releases on function exit,
                        // so alloca retains are "leaked" in both backends.
                        bbOut().instrs.push_back(MInstr{
                            MOpcode::MovRR,
                            {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, v)}});
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_retain_maybe")}});
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::StrRegFpImm,
                                   {MOperand::vregOp(RegClass::GPR, v), MOperand::immOp(off)}});
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
                                           ctx.tempRegClass,
                                           ctx.nextVRegId,
                                           vbase,
                                           cbase) &&
                    materializeValueToVReg(ins.operands[1],
                                           bbIn,
                                           ctx.ti,
                                           ctx.fb,
                                           bbOut(),
                                           ctx.tempVReg,
                                           ctx.tempRegClass,
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
                    else if (isStr)
                    {
                        // String store: release old, retain new, then store
                        // 1. Load old string from [base] into temp
                        const uint16_t vOld = ctx.nextVRegId++;
                        bbOut().instrs.push_back(MInstr{MOpcode::LdrRegBaseImm,
                                                        {MOperand::vregOp(RegClass::GPR, vOld),
                                                         MOperand::vregOp(RegClass::GPR, vbase),
                                                         MOperand::immOp(0)}});
                        // 2. Call rt_str_release_maybe(old) - move to X0 and call
                        bbOut().instrs.push_back(MInstr{
                            MOpcode::MovRR,
                            {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, vOld)}});
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_release_maybe")}});
                        // 3. Call rt_str_retain_maybe(new) - move to X0 and call
                        bbOut().instrs.push_back(MInstr{
                            MOpcode::MovRR,
                            {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, vval)}});
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_retain_maybe")}});
                        // 4. Store the new string
                        bbOut().instrs.push_back(MInstr{MOpcode::StrRegBaseImm,
                                                        {MOperand::vregOp(RegClass::GPR, vval),
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
                                        ctx.tempRegClass,
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
                                           ctx.tempRegClass,
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
                                           ctx.tempRegClass,
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
            if (lowerCallWithArgs(ins,
                                  bbIn,
                                  ctx.ti,
                                  ctx.fb,
                                  bbOut(),
                                  seq,
                                  ctx.tempVReg,
                                  ctx.tempRegClass,
                                  ctx.nextVRegId))
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
                        // String results must be retained immediately after the call.
                        // Functions like rt_str_concat consume (unref) their string
                        // arguments; without this retain a string used between Call
                        // and its first alloca store has an unbalanced refcount and
                        // will be freed prematurely (BUG-NAT-005).
                        if (ins.type.kind == il::core::Type::Kind::Str)
                        {
                            bbOut().instrs.push_back(
                                MInstr{MOpcode::MovRR,
                                       {MOperand::regOp(PhysReg::X0),
                                        MOperand::vregOp(RegClass::GPR, dst)}});
                            bbOut().instrs.push_back(
                                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_retain_maybe")}});
                        }
                        // Bug #1 fix: mask boolean return values to a single bit.
                        // Per AAPCS64, a C function returning bool only guarantees
                        // the low 8 bits of w0 are meaningful.
                        if (ins.type.kind == il::core::Type::Kind::I1)
                        {
                            const uint16_t mask = ctx.nextVRegId++;
                            bbOut().instrs.push_back(MInstr{
                                MOpcode::MovRI,
                                {MOperand::vregOp(RegClass::GPR, mask), MOperand::immOp(1)}});
                            const uint16_t masked = ctx.nextVRegId++;
                            bbOut().instrs.push_back(
                                MInstr{MOpcode::AndRRR,
                                       {MOperand::vregOp(RegClass::GPR, masked),
                                        MOperand::vregOp(RegClass::GPR, dst),
                                        MOperand::vregOp(RegClass::GPR, mask)}});
                            ctx.tempVReg[*ins.result] = masked;
                        }
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
                // WARNING: This path should NOT be reached for normal function calls
                // with arguments. If we get here for a call with args, it means
                // lowerCallWithArgs failed to materialize an argument.
                if (!ins.operands.empty())
                {
                    fprintf(stderr,
                            "WARNING: lowerCallWithArgs failed for %s with %zu args\n",
                            ins.callee.c_str(),
                            ins.operands.size());
                }
                bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp(ins.callee)}});
            }
            return true;
        }
        case Opcode::CallIndirect:
        {
            // CallIndirect: operands[0] is function pointer (can be GlobalAddr @symbol or %temp)
            // operands[1..n] are arguments

            if (ins.operands.empty())
                return true; // Invalid - need at least a function pointer

            uint16_t vFuncPtr = 0;
            size_t argStartIdx = 1; // Arguments start after the function pointer

            const auto &funcPtrOp = ins.operands[0];
            if (funcPtrOp.kind == il::core::Value::Kind::GlobalAddr)
            {
                // operands[0] is @symbol - load address via adrp/add pattern
                const uint16_t addrReg = ctx.nextVRegId++;
                bbOut().instrs.push_back(MInstr{
                    MOpcode::AdrPage,
                    {MOperand::vregOp(RegClass::GPR, addrReg), MOperand::labelOp(funcPtrOp.str)}});
                bbOut().instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                {MOperand::vregOp(RegClass::GPR, addrReg),
                                                 MOperand::vregOp(RegClass::GPR, addrReg),
                                                 MOperand::labelOp(funcPtrOp.str)}});
                vFuncPtr = addrReg;
            }
            else
            {
                // operands[0] is a temporary holding the function pointer
                RegClass cFuncPtr = RegClass::GPR;
                if (!materializeValueToVReg(ins.operands[0],
                                            bbIn,
                                            ctx.ti,
                                            ctx.fb,
                                            bbOut(),
                                            ctx.tempVReg,
                                            ctx.tempRegClass,
                                            ctx.nextVRegId,
                                            vFuncPtr,
                                            cFuncPtr))
                {
                    return true; // Can't materialize function pointer
                }
            }

            // Move arguments to arg registers x0-x7
            constexpr PhysReg argRegs[] = {PhysReg::X0,
                                           PhysReg::X1,
                                           PhysReg::X2,
                                           PhysReg::X3,
                                           PhysReg::X4,
                                           PhysReg::X5,
                                           PhysReg::X6,
                                           PhysReg::X7};
            const size_t numArgs = ins.operands.size() - argStartIdx;
            for (size_t i = 0; i < numArgs && i < 8; ++i)
            {
                uint16_t vArg = 0;
                RegClass cArg = RegClass::GPR;
                if (materializeValueToVReg(ins.operands[argStartIdx + i],
                                           bbIn,
                                           ctx.ti,
                                           ctx.fb,
                                           bbOut(),
                                           ctx.tempVReg,
                                           ctx.tempRegClass,
                                           ctx.nextVRegId,
                                           vArg,
                                           cArg))
                {
                    bbOut().instrs.push_back(MInstr{
                        MOpcode::MovRR,
                        {MOperand::regOp(argRegs[i]), MOperand::vregOp(RegClass::GPR, vArg)}});
                }
            }

            // Move function pointer to a caller-saved register (x9) before the call
            // to avoid it being clobbered by arg setup
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRR,
                       {MOperand::regOp(PhysReg::X9), MOperand::vregOp(RegClass::GPR, vFuncPtr)}});

            // Emit indirect call: blr x9
            bbOut().instrs.push_back(MInstr{MOpcode::Blr, {MOperand::regOp(PhysReg::X9)}});

            // If the call produces a result, move x0 to a fresh vreg
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
                    // String results from indirect calls also need an immediate retain
                    // for the same reason as direct calls (BUG-NAT-005).
                    if (ins.type.kind == il::core::Type::Kind::Str)
                    {
                        bbOut().instrs.push_back(MInstr{
                            MOpcode::MovRR,
                            {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, dst)}});
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_retain_maybe")}});
                    }
                    // Bug #1 fix: mask boolean return values from indirect calls
                    if (ins.type.kind == il::core::Type::Kind::I1)
                    {
                        const uint16_t mask = ctx.nextVRegId++;
                        bbOut().instrs.push_back(
                            MInstr{MOpcode::MovRI,
                                   {MOperand::vregOp(RegClass::GPR, mask), MOperand::immOp(1)}});
                        const uint16_t masked = ctx.nextVRegId++;
                        bbOut().instrs.push_back(MInstr{MOpcode::AndRRR,
                                                        {MOperand::vregOp(RegClass::GPR, masked),
                                                         MOperand::vregOp(RegClass::GPR, dst),
                                                         MOperand::vregOp(RegClass::GPR, mask)}});
                        ctx.tempVReg[*ins.result] = masked;
                    }
                }
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
                                                 ctx.tempRegClass,
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
            // Bug #3 fix: for void main, zero x0 so the process exit code is 0
            if (ins.operands.empty() && ctx.mf.name == "main")
            {
                bbOut().instrs.push_back(
                    MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
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

        // === Structured Error Handling (not yet supported in native codegen) ===
        case Opcode::TrapKind:
        case Opcode::TrapErr:
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetIp:
        case Opcode::ErrGetLine:
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            fprintf(stderr,
                    "ERROR: AArch64 native codegen does not yet support structured "
                    "error handling (opcode: %s). Use VM execution for programs "
                    "using try/catch.\n",
                    il::core::toString(ins.op));
            return true;

        default:
            // Opcode not handled - caller should process
            return false;
    }
}

} // namespace viper::codegen::aarch64
