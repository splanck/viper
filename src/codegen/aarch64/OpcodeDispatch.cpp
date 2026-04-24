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
/// that branch to dedicated trap helpers or a NULL-message `rt_trap` path:
/// ```
/// Block:                    Trap Block:
/// cmp original, widened     .Ltrap_cast_N:
/// b.ne .Ltrap_cast_N    →     bl rt_trap_ovf
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

#include <cstring>
#include <stdexcept>

namespace viper::codegen::aarch64 {

using il::core::Opcode;

static const char *condForOpcode(Opcode op) {
    return lookupCondition(op);
}

static void moveValueToArg(const il::core::Value &value,
                           const il::core::BasicBlock &bbIn,
                           LoweringContext &ctx,
                           PhysReg dstReg,
                           MBasicBlock &out,
                           const char *what) {
    uint16_t src = 0;
    RegClass cls = RegClass::GPR;
    if (!materializeValueToVReg(value, bbIn, ctx, out, src, cls))
        throw std::runtime_error(std::string("AArch64 lowering: failed to materialize ") + what);
    if (cls != RegClass::GPR)
        throw std::runtime_error(std::string("AArch64 lowering: expected GPR for ") + what);
    out.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(dstReg), MOperand::vregOp(RegClass::GPR, src)}});
}

static void captureGprCallResult(const il::core::Instr &ins,
                                 LoweringContext &ctx,
                                 MBasicBlock &out) {
    if (!ins.result)
        return;
    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    ctx.tempRegClass[*ins.result] = RegClass::GPR;
    out.instrs.push_back(MInstr{
        MOpcode::MovRR, {MOperand::vregOp(RegClass::GPR, dst), MOperand::regOp(PhysReg::X0)}});
}

static uint64_t f64Bits(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint16_t materializeF64Constant(double value, LoweringContext &ctx, MBasicBlock &out) {
    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    const uint16_t bitsGpr = allocateNextVReg(ctx.nextVRegId);
    out.instrs.push_back(MInstr{MOpcode::MovRI,
                                {MOperand::vregOp(RegClass::GPR, bitsGpr),
                                 MOperand::immOp(static_cast<long long>(f64Bits(value)))}});
    out.instrs.push_back(MInstr{MOpcode::FMovGR,
                                {MOperand::vregOp(RegClass::FPR, dst),
                                 MOperand::vregOp(RegClass::GPR, bitsGpr)}});
    return dst;
}

static int integerTypeBits(il::core::Type::Kind kind) {
    switch (kind) {
        case il::core::Type::Kind::I16:
            return 16;
        case il::core::Type::Kind::I32:
            return 32;
        case il::core::Type::Kind::I64:
            return 64;
        default:
            throw std::runtime_error("AArch64 lowering: checked fp cast requires integer result");
    }
}

static double signedLowerBoundForBits(int bits) {
    switch (bits) {
        case 16:
            return -32768.0;
        case 32:
            return -2147483648.0;
        case 64:
            return -9223372036854775808.0;
        default:
            throw std::runtime_error("AArch64 lowering: unsupported signed fp cast width");
    }
}

static double signedUpperExclusiveForBits(int bits) {
    switch (bits) {
        case 16:
            return 32768.0;
        case 32:
            return 2147483648.0;
        case 64:
            return 9223372036854775808.0;
        default:
            throw std::runtime_error("AArch64 lowering: unsupported signed fp cast width");
    }
}

static double unsignedUpperExclusiveForBits(int bits) {
    switch (bits) {
        case 16:
            return 65536.0;
        case 32:
            return 4294967296.0;
        case 64:
            return 18446744073709551616.0;
        default:
            throw std::runtime_error("AArch64 lowering: unsupported unsigned fp cast width");
    }
}

static void emitTrapRaiseErrorBlock(MFunction &mf, std::string label, int code) {
    mf.blocks.emplace_back();
    auto &trapBlock = mf.blocks.back();
    trapBlock.name = std::move(label);
    trapBlock.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(code)}});
    trapBlock.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_raise_error")}});
}

bool lowerInstruction(const il::core::Instr &ins,
                      const il::core::BasicBlock &bbIn,
                      LoweringContext &ctx,
                      std::size_t bbOutIdx) {
    // Helper lambda to access the output block by index.
    // This ensures we always get a valid reference even after emplace_back().
    auto bbOut = [&]() -> MBasicBlock & { return ctx.mf.blocks[bbOutIdx]; };

    switch (ins.op) {
        case Opcode::Zext1:
        case Opcode::Trunc1: {
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
                return false;
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            const uint16_t one = allocateNextVReg(ctx.nextVRegId);
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, one), MOperand::immOp(1)}});
            bbOut().instrs.push_back(MInstr{MOpcode::AndRRR,
                                            {MOperand::vregOp(RegClass::GPR, dst),
                                             MOperand::vregOp(RegClass::GPR, sv),
                                             MOperand::vregOp(RegClass::GPR, one)}});
            return true;
        }
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk: {
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
            const uint16_t vt = allocateNextVReg(ctx.nextVRegId);
            if (sh > 0) {
                bbOut().instrs.push_back(MInstr{
                    MOpcode::MovRR,
                    {MOperand::vregOp(RegClass::GPR, vt), MOperand::vregOp(RegClass::GPR, sv)}});
                if (ins.op == Opcode::CastSiNarrowChk) {
                    bbOut().instrs.push_back(MInstr{MOpcode::LslRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                    bbOut().instrs.push_back(MInstr{MOpcode::AsrRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                } else {
                    bbOut().instrs.push_back(MInstr{MOpcode::LslRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                    bbOut().instrs.push_back(MInstr{MOpcode::LsrRI,
                                                    {MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::vregOp(RegClass::GPR, vt),
                                                     MOperand::immOp(sh)}});
                }
            } else {
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
                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_ovf")}});
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            bbOut().instrs.push_back(MInstr{
                MOpcode::MovRR,
                {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::GPR, vt)}});
            return true;
        }
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk: {
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
            if (fcls != RegClass::FPR)
                return false;

            const uint16_t rounded = allocateNextVReg(ctx.nextVRegId);
            bbOut().instrs.push_back(MInstr{
                MOpcode::FRintN,
                {MOperand::vregOp(RegClass::FPR, rounded), MOperand::vregOp(RegClass::FPR, fv)}});

            const std::string trapLabel =
                ".Ltrap_fpcast_invalid_" + std::to_string(ctx.trapLabelCounter++);
            const std::string overflowLabel =
                ".Ltrap_fpcast_ovf_" + std::to_string(ctx.trapLabelCounter++);

            // NaN becomes unordered; trap before any range comparisons.
            bbOut().instrs.push_back(MInstr{
                MOpcode::FCmpRR,
                {MOperand::vregOp(RegClass::FPR, rounded), MOperand::vregOp(RegClass::FPR, rounded)}});
            bbOut().instrs.push_back(
                MInstr{MOpcode::BCond, {MOperand::condOp("vs"), MOperand::labelOp(trapLabel)}});

            const int resultBits = integerTypeBits(ins.type.kind);
            if (ins.op == Opcode::CastFpToSiRteChk) {
                const uint16_t lowerBound =
                    materializeF64Constant(signedLowerBoundForBits(resultBits), ctx, bbOut());
                const uint16_t upperBound =
                    materializeF64Constant(signedUpperExclusiveForBits(resultBits), ctx, bbOut());
                bbOut().instrs.push_back(MInstr{
                    MOpcode::FCmpRR,
                    {MOperand::vregOp(RegClass::FPR, rounded),
                     MOperand::vregOp(RegClass::FPR, lowerBound)}});
                bbOut().instrs.push_back(
                    MInstr{MOpcode::BCond, {MOperand::condOp("lt"), MOperand::labelOp(overflowLabel)}});
                bbOut().instrs.push_back(MInstr{
                    MOpcode::FCmpRR,
                    {MOperand::vregOp(RegClass::FPR, rounded),
                     MOperand::vregOp(RegClass::FPR, upperBound)}});
                bbOut().instrs.push_back(
                    MInstr{MOpcode::BCond, {MOperand::condOp("ge"), MOperand::labelOp(overflowLabel)}});
            } else {
                const uint16_t zero = materializeF64Constant(0.0, ctx, bbOut());
                const uint16_t upperBound =
                    materializeF64Constant(unsignedUpperExclusiveForBits(resultBits), ctx, bbOut());
                bbOut().instrs.push_back(MInstr{
                    MOpcode::FCmpRR,
                    {MOperand::vregOp(RegClass::FPR, rounded),
                     MOperand::vregOp(RegClass::FPR, zero)}});
                bbOut().instrs.push_back(
                    MInstr{MOpcode::BCond, {MOperand::condOp("lt"), MOperand::labelOp(trapLabel)}});
                bbOut().instrs.push_back(MInstr{
                    MOpcode::FCmpRR,
                    {MOperand::vregOp(RegClass::FPR, rounded),
                     MOperand::vregOp(RegClass::FPR, upperBound)}});
                bbOut().instrs.push_back(
                    MInstr{MOpcode::BCond, {MOperand::condOp("ge"), MOperand::labelOp(overflowLabel)}});
            }

            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::GPR;
            if (ins.op == Opcode::CastFpToSiRteChk) {
                bbOut().instrs.push_back(MInstr{MOpcode::FCvtZS,
                                                {MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::vregOp(RegClass::FPR, rounded)}});
            } else {
                bbOut().instrs.push_back(MInstr{MOpcode::FCvtZU,
                                                {MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::vregOp(RegClass::FPR, rounded)}});
            }
            emitTrapRaiseErrorBlock(ctx.mf, trapLabel, 5);
            emitTrapRaiseErrorBlock(ctx.mf, overflowLabel, 4);
            return true;
        }
        case Opcode::CastSiToFp:
        case Opcode::CastUiToFp: {
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
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
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
            return lowerSRemChk0(ins, bbIn, ctx, bbOut());
        case Opcode::SDivChk0:
            return lowerSDivChk0(ins, bbIn, ctx, bbOut());
        case Opcode::UDivChk0:
            return lowerUDivChk0(ins, bbIn, ctx, bbOut());
        case Opcode::URemChk0:
            return lowerURemChk0(ins, bbIn, ctx, bbOut());
        case Opcode::IdxChk:
            return lowerIdxChk(ins, bbIn, ctx, bbOut());
        case Opcode::SRem:
            return lowerSRem(ins, bbIn, ctx, bbOut());
        case Opcode::URem:
            return lowerURem(ins, bbIn, ctx, bbOut());
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            return lowerFpArithmetic(ins, bbIn, ctx, bbOut());
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
        case Opcode::FCmpOrd:
        case Opcode::FCmpUno:
            return lowerFpCompare(ins, bbIn, ctx, bbOut());
        case Opcode::Sitofp:
            return lowerSitofp(ins, bbIn, ctx, bbOut());
        case Opcode::Fptosi:
            return lowerFptosi(ins, bbIn, ctx, bbOut());
        case Opcode::ConstF64: {
            // Lower const.f64 by materializing a floating-point constant.
            // We load the 64-bit IEEE-754 representation into a GPR and then
            // use fmov to transfer to FPR.  Operands may arrive as ConstFloat
            // (when parsed from a float literal like 1.0) or ConstInt (when
            // parsed from the integer bit-pattern encoding used by the IL
            // serializer for programmatically-constructed modules).
            if (!ins.result || ins.operands.empty())
                return true;

            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::FPR;

            uint64_t bits = 0;
            if (ins.operands[0].kind == il::core::Value::Kind::ConstFloat) {
                const double fval = ins.operands[0].f64;
                std::memcpy(&bits, &fval, sizeof(bits));
            } else if (ins.operands[0].kind == il::core::Value::Kind::ConstInt) {
                // Integer operand holds IEEE-754 bit pattern directly.
                bits = static_cast<uint64_t>(ins.operands[0].i64);
            } else {
                return false;
            }

            const uint16_t tmpGpr = allocateNextVReg(ctx.nextVRegId);
            bbOut().instrs.push_back(MInstr{MOpcode::MovRI,
                                            {MOperand::vregOp(RegClass::GPR, tmpGpr),
                                             MOperand::immOp(static_cast<long long>(bits))}});
            bbOut().instrs.push_back(MInstr{
                MOpcode::FMovGR,
                {MOperand::vregOp(RegClass::FPR, dst), MOperand::vregOp(RegClass::GPR, tmpGpr)}});
            return true;
        }
        case Opcode::ConstNull: {
            // const_null produces a null pointer (0)
            if (!ins.result)
                return true;
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::GPR;
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, dst), MOperand::immOp(0)}});
            return true;
        }
        case Opcode::GAddr: {
            // gaddr @symbol produces the address of a global variable
            if (!ins.result || ins.operands.empty())
                return true;
            if (ins.operands[0].kind != il::core::Value::Kind::GlobalAddr)
                return true;
            const std::string &sym = ins.operands[0].str;
            // Materialize address of global symbol using adrp+add
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
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
        case Opcode::ConstStr: {
            // Lower const_str to produce a runtime string handle from a pooled literal.
            // This must be lowered proactively (not demand-lowered) when the result
            // is a cross-block temp that will be spilled.
            if (!ins.result || ins.operands.empty())
                return true;
            if (ins.operands[0].kind != il::core::Value::Kind::GlobalAddr)
                return true;
            const std::string &sym = ins.operands[0].str;
            const uint16_t dst =
                emitConstStrGlobalToVReg(sym, ctx.stringLiteralByteLengths, bbOut(), ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            ctx.tempRegClass[*ins.result] = RegClass::GPR;
            return true;
        }
        case Opcode::AddrOf: {
            if (!ins.result || ins.operands.empty())
                return true;

            if (ins.operands[0].kind == il::core::Value::Kind::GlobalAddr) {
                const std::string &sym = ins.operands[0].str;
                const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
                ctx.tempVReg[*ins.result] = dst;
                ctx.tempRegClass[*ins.result] = RegClass::GPR;
                bbOut().instrs.push_back(MInstr{
                    MOpcode::AdrPage,
                    {MOperand::vregOp(RegClass::GPR, dst), MOperand::labelOp(sym)}});
                bbOut().instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                {MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::labelOp(sym)}});
                return true;
            }

            if (ins.operands[0].kind == il::core::Value::Kind::Temp) {
                const int offset = ctx.fb.localOffset(ins.operands[0].id);
                if (offset != 0) {
                    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
                    ctx.tempVReg[*ins.result] = dst;
                    ctx.tempRegClass[*ins.result] = RegClass::GPR;
                    bbOut().instrs.push_back(MInstr{
                        MOpcode::AddFpImm,
                        {MOperand::vregOp(RegClass::GPR, dst), MOperand::immOp(offset)}});
                    return true;
                }
            }

            return false;
        }
        case Opcode::Store:
            return lowerStore(ins, bbIn, ctx, bbOut());
        case Opcode::GEP:
            return lowerGEP(ins, bbIn, ctx, bbOut());
        case Opcode::Load:
            return lowerLoad(ins, bbIn, ctx, bbOut());
        case Opcode::Call:
            lowerCall(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::CallIndirect:
            return lowerCallIndirect(ins, bbIn, ctx, bbOut());
        case Opcode::Ret:
            lowerRet(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Alloca:
            // Alloca is handled during frame building, no MIR needed here
            return true;
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::SwitchI32:
            // Terminators are lowered in a separate pass after all instructions
            return true;

        // === Structured Error Handling ===
        // NativeEHLowering rewrites structured EH into helper calls and plain
        // control flow before backend lowering. If raw EH markers still reach
        // this stage, treat them as inert markers rather than generating new
        // machine semantics.
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
            return true;

        case Opcode::Trap:
            if (ins.operands.empty()) {
                bbOut().instrs.push_back(
                    MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
            } else {
                moveValueToArg(ins.operands[0], bbIn, ctx, PhysReg::X0, bbOut(), "trap message");
            }
            bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
            return true;

        case Opcode::TrapKind:
            bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_get_kind")}});
            captureGprCallResult(ins, ctx, bbOut());
            return true;

        // trap.err constructs the current error payload and returns an opaque token.
        case Opcode::TrapErr: {
            if (ins.operands.size() < 2)
                throw std::runtime_error(
                    "AArch64 lowering: trap.err expects code and text operands");
            moveValueToArg(ins.operands[0], bbIn, ctx, PhysReg::X0, bbOut(), "trap.err code");
            moveValueToArg(ins.operands[1], bbIn, ctx, PhysReg::X1, bbOut(), "trap.err message");
            bbOut().instrs.push_back(
                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_error_make")}});
            captureGprCallResult(ins, ctx, bbOut());
            return true;
        }

        // Error field accessors call runtime TLS bridge functions to retrieve
        // trap classification stored by rt_trap_fields_set() before the trap.
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetLine: {
            const char *rtFunc = nullptr;
            switch (ins.op) {
                case Opcode::ErrGetKind:
                    rtFunc = "rt_trap_get_kind";
                    break;
                case Opcode::ErrGetCode:
                    rtFunc = "rt_trap_get_code";
                    break;
                case Opcode::ErrGetLine:
                    rtFunc = "rt_trap_get_line";
                    break;
                default:
                    rtFunc = "rt_trap_get_kind";
                    break;
            }
            bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp(rtFunc)}});
            captureGprCallResult(ins, ctx, bbOut());
            return true;
        }
        case Opcode::ErrGetIp: {
            bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_get_ip")}});
            captureGprCallResult(ins, ctx, bbOut());
            return true;
        }
        case Opcode::ErrGetMsg: {
            bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_throw_msg_get")}});
            captureGprCallResult(ins, ctx, bbOut());
            return true;
        }

        // resume.label is a branch to an explicit target label.
        // The resume token operand is ignored in native codegen.
        // Handled as a terminator in TerminatorLowering.cpp.
        case Opcode::TrapFromErr: {
            if (ins.operands.empty()) {
                bbOut().instrs.push_back(
                    MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
            } else {
                moveValueToArg(
                    ins.operands[0], bbIn, ctx, PhysReg::X0, bbOut(), "trap.from_err code");
            }
            bbOut().instrs.push_back(
                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_raise_error")}});
            return true;
        }
        case Opcode::ResumeLabel:
            return true;

        // resume.same / resume.next must have been lowered away by the shared
        // native EH rewrite before they reach backend instruction selection.
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
            throw std::runtime_error(
                std::string("AArch64 lowering received raw ") + il::core::toString(ins.op) +
                " after NativeEHLowering; structured EH rewrite is incomplete.");

        default:
            // Opcode not handled - caller should process
            return false;
    }
}

} // namespace viper::codegen::aarch64
