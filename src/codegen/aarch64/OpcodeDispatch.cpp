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
                return true;
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
                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            bbOut().instrs.push_back(MInstr{
                MOpcode::MovRR,
                {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::GPR, vt)}});
            return true;
        }
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk: {
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
            const uint16_t rounded = allocateNextVReg(ctx.nextVRegId);
            bbOut().instrs.push_back(MInstr{
                MOpcode::FRintN,
                {MOperand::vregOp(RegClass::FPR, rounded), MOperand::vregOp(RegClass::FPR, fv)}});

            // Convert rounded value to integer
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            if (ins.op == Opcode::CastFpToSiRteChk) {
                bbOut().instrs.push_back(MInstr{MOpcode::FCvtZS,
                                                {MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::vregOp(RegClass::FPR, rounded)}});
            } else {
                bbOut().instrs.push_back(MInstr{MOpcode::FCvtZU,
                                                {MOperand::vregOp(RegClass::GPR, dst),
                                                 MOperand::vregOp(RegClass::FPR, rounded)}});
            }
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
            // Lower const_str to produce a string handle via rt_const_cstr.
            // This must be lowered proactively (not demand-lowered) when the result
            // is a cross-block temp that will be spilled.
            if (!ins.result || ins.operands.empty())
                return true;
            if (ins.operands[0].kind != il::core::Value::Kind::GlobalAddr)
                return true;
            const std::string &sym = ins.operands[0].str;
            // Materialize address of pooled literal label
            const uint16_t litPtrV = allocateNextVReg(ctx.nextVRegId);
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
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            ctx.tempVReg[*ins.result] = dst;
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRR,
                       {MOperand::vregOp(RegClass::GPR, dst), MOperand::regOp(PhysReg::X0)}});
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

            return true;
        }
        case Opcode::Store:
            lowerStore(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::GEP:
            lowerGEP(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Load:
            lowerLoad(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Call:
            lowerCall(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::CallIndirect:
            lowerCallIndirect(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Ret:
            lowerRet(ins, bbIn, ctx, bbOut());
            return true;
        case Opcode::Alloca:
            // Alloca is handled during frame building, no MIR needed here
            return true;
        case Opcode::Br:
        case Opcode::CBr:
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

        // Bare trap — no message available, pass NULL to rt_trap.
        case Opcode::Trap:
            bbOut().instrs.push_back(
                MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
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

        // resume.label is a branch to an explicit target label.
        // The resume token operand is ignored in native codegen.
        // Handled as a terminator in TerminatorLowering.cpp.
        case Opcode::TrapFromErr:
            return true;
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
