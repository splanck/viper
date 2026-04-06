//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file InstrLowering.cpp
/// @brief Opcode-specific lowering handlers for IL to MIR conversion.
///
/// This file implements the instruction lowering logic that converts individual
/// IL instructions into sequences of AArch64 MIR instructions. Each IL opcode
/// has a corresponding handler that emits the appropriate machine instructions.
///
/// **What is Instruction Lowering?**
/// Instruction lowering translates high-level IL operations into low-level
/// machine operations. A single IL instruction may expand to multiple MIR
/// instructions depending on the operation complexity and available hardware.
///
/// **Lowering Examples:**
/// ```
/// IL: %1 = add.i64 %0, 42
/// MIR: v1 = MovRI #42        ; materialize constant
///      v2 = AddRRR v0, v1    ; actual addition
///
/// IL: %1 = mul.i64 %0, 8
/// MIR: v1 = LslRI v0, #3     ; strength reduce: x * 8 = x << 3
///
/// IL: %1 = srem.i64 %0, %2
/// MIR: v3 = SDivRRR v0, v2   ; quotient = a / b
///      v1 = MSubRRRR v0, v3, v2  ; remainder = a - (quotient * b)
/// ```
///
/// **Value Materialization:**
/// Before an IL value can be used as an MIR operand, it must be "materialized"
/// into a virtual register:
///
/// | IL Value Kind | Materialization                                |
/// |---------------|------------------------------------------------|
/// | Temp          | Look up in tempVReg map or reload from spill  |
/// | ConstInt      | MovRI (immediate to register)                  |
/// | ConstFloat    | Load from rodata pool via AdrPage + LdrFprBaseImm |
/// | GlobalAddr    | AdrPage + AddPageOff (PC-relative addressing)  |
/// | NullPtr       | MovRI #0                                       |
///
/// **Register Class Selection:**
/// Operands are classified by their IL type:
/// | IL Type | Register Class | Physical Registers |
/// |---------|----------------|-------------------|
/// | i1-i64  | GPR            | x0-x28            |
/// | ptr     | GPR            | x0-x28            |
/// | f64     | FPR            | d0-d31            |
///
/// **Comparison Lowering:**
/// IL comparison opcodes (icmp_*, fcmp_*) lower to:
/// 1. CmpRR/FCmpRR - set condition flags
/// 2. Cset - materialize flag as 0/1 in result register
///
/// **Condition Codes:**
/// | IL Opcode  | AArch64 Condition |
/// |------------|-------------------|
/// | ICmpEQ     | eq                |
/// | ICmpNE     | ne                |
/// | ICmpSLT    | lt                |
/// | ICmpSLE    | le                |
/// | ICmpSGT    | gt                |
/// | ICmpSGE    | ge                |
/// | ICmpULT    | lo                |
/// | ICmpUGT    | hi                |
///
/// **Handler Organization:**
/// Handlers are grouped by category:
/// - Arithmetic: add, sub, mul, div, rem
/// - Bitwise: and, or, xor, shl, shr
/// - Comparison: icmp_*, fcmp_*
/// - Conversion: sext, zext, trunc, sitofp, fptosi
/// - Memory: load, store, alloca
/// - Call: call instruction lowering
///
/// @see LowerILToMIR.cpp For main lowering orchestration
/// @see OpcodeDispatch.cpp For opcode-to-handler dispatch
/// @see OpcodeMappings.hpp For opcode-to-condition tables
///
//===----------------------------------------------------------------------===//

#include "InstrLowering.hpp"
#include "A64ImmediateUtils.hpp"
#include "FrameBuilder.hpp"
#include "OpcodeMappings.hpp"
#include "codegen/common/CallArgLayout.hpp"

#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace viper::codegen::aarch64 {

//===----------------------------------------------------------------------===//
// Helper: Get condition code for comparison opcodes
//===----------------------------------------------------------------------===//

static const char *condForOpcode(il::core::Opcode op) {
    return lookupCondition(op);
}

static const char *fpCondCode(il::core::Opcode op) {
    switch (op) {
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
        case il::core::Opcode::FCmpOrd:
            return "vc"; // vc = overflow clear (V flag clear if neither NaN)
        case il::core::Opcode::FCmpUno:
            return "vs"; // vs = overflow set (V flag set if either NaN)
        default:
            return "eq";
    }
}

namespace {

struct MaterializedCallArg {
    uint16_t vreg{0};
    viper::codegen::common::CallArgClass cls{viper::codegen::common::CallArgClass::GPR};
};

uint16_t emitMaskedI1Value(MBasicBlock &out, uint16_t srcVReg, uint16_t &nextVRegId) {
    const uint16_t mask = allocateNextVReg(nextVRegId);
    out.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, mask), MOperand::immOp(1)}});
    const uint16_t masked = allocateNextVReg(nextVRegId);
    out.instrs.push_back(MInstr{MOpcode::AndRRR,
                                {MOperand::vregOp(RegClass::GPR, masked),
                                 MOperand::vregOp(RegClass::GPR, srcVReg),
                                 MOperand::vregOp(RegClass::GPR, mask)}});
    return masked;
}

bool marshalCallArgs(const std::vector<MaterializedCallArg> &args,
                     std::size_t numNamedArgs,
                     bool variadicTailOnStack,
                     const TargetInfo &ti,
                     LoweredCall &seq) {
    using namespace viper::codegen::common;

    std::vector<CallArg> planArgs;
    planArgs.reserve(args.size());
    for (const auto &arg : args) {
        CallArg planArg{};
        planArg.cls = arg.cls;
        planArg.vreg = arg.vreg;
        planArgs.push_back(planArg);
    }

    const CallArgLayout layout =
        planCallArgs(planArgs,
                     CallArgLayoutConfig{.maxGPRArgs = ti.intArgOrder.size(),
                                         .maxFPRArgs = ti.f64ArgOrder.size(),
                                         .slotModel = CallSlotModel::IndependentRegisterBanks,
                                         .variadicTailOnStack = variadicTailOnStack,
                                         .numNamedArgs = numNamedArgs});

    const std::size_t stackBytes = ((layout.stackSlotsUsed * 8 + 15) / 16) * 16;
    if (stackBytes > 0) {
        seq.prefix.push_back(
            MInstr{MOpcode::SubSpImm, {MOperand::immOp(static_cast<long long>(stackBytes))}});
    }

    for (const auto &loc : layout.locations) {
        const auto &arg = args[loc.argIndex];
        if (loc.inRegister) {
            if (loc.cls == CallArgClass::FPR) {
                const PhysReg dst = ti.f64ArgOrder[loc.regIndex];
                seq.prefix.push_back(
                    MInstr{MOpcode::FMovRR,
                           {MOperand::regOp(dst), MOperand::vregOp(RegClass::FPR, arg.vreg)}});
            } else {
                const PhysReg dst = ti.intArgOrder[loc.regIndex];
                seq.prefix.push_back(
                    MInstr{MOpcode::MovRR,
                           {MOperand::regOp(dst), MOperand::vregOp(RegClass::GPR, arg.vreg)}});
            }
        } else {
            const long long stackOffset = static_cast<long long>(loc.stackSlotIndex * 8);
            const MOpcode storeOpc =
                (loc.cls == CallArgClass::FPR) ? MOpcode::StrFprSpImm : MOpcode::StrRegSpImm;
            seq.prefix.push_back(
                MInstr{storeOpc,
                       {(loc.cls == CallArgClass::FPR) ? MOperand::vregOp(RegClass::FPR, arg.vreg)
                                                       : MOperand::vregOp(RegClass::GPR, arg.vreg),
                        MOperand::immOp(stackOffset)}});
        }
    }

    if (stackBytes > 0) {
        seq.postfix.push_back(
            MInstr{MOpcode::AddSpImm, {MOperand::immOp(static_cast<long long>(stackBytes))}});
    }
    return true;
}

uint16_t captureCallResult(const il::core::Instr &ins,
                           const TargetInfo &ti,
                           MBasicBlock &out,
                           std::unordered_map<unsigned, uint16_t> &tempVReg,
                           std::unordered_map<unsigned, RegClass> &tempRegClass,
                           uint16_t &nextVRegId) {
    if (!ins.result)
        return 0;

    const uint16_t dst = allocateNextVReg(nextVRegId);
    tempVReg[*ins.result] = dst;
    if (ins.type.kind == il::core::Type::Kind::F64) {
        tempRegClass[*ins.result] = RegClass::FPR;
        out.instrs.push_back(
            MInstr{MOpcode::FMovRR,
                   {MOperand::vregOp(RegClass::FPR, dst), MOperand::regOp(ti.f64ReturnReg)}});
        return dst;
    }

    out.instrs.push_back(MInstr{
        MOpcode::MovRR, {MOperand::vregOp(RegClass::GPR, dst), MOperand::regOp(ti.intReturnReg)}});
    if (ins.type.kind == il::core::Type::Kind::Str) {
        out.instrs.push_back(
            MInstr{MOpcode::MovRR,
                   {MOperand::regOp(ti.intReturnReg), MOperand::vregOp(RegClass::GPR, dst)}});
        out.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_retain_maybe")}});
    }
    if (ins.type.kind == il::core::Type::Kind::I1) {
        const uint16_t masked = emitMaskedI1Value(out, dst, nextVRegId);
        tempVReg[*ins.result] = masked;
        return masked;
    }
    return dst;
}

void retainStringVReg(MBasicBlock &out, uint16_t strVReg) {
    out.instrs.push_back(
        MInstr{MOpcode::MovRR,
               {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, strVReg)}});
    out.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_retain_maybe")}});
}

std::optional<std::size_t> lookupStringLiteralByteLen(
    const std::string &sym,
    const std::unordered_map<std::string, std::size_t> *stringLiteralByteLengths) {
    if (!stringLiteralByteLengths)
        return std::nullopt;
    const auto it = stringLiteralByteLengths->find(sym);
    if (it == stringLiteralByteLengths->end())
        return std::nullopt;
    return it->second;
}

} // namespace

void emitConstStrGlobalToX0(const std::string &sym,
                            const std::unordered_map<std::string, std::size_t>
                                *stringLiteralByteLengths,
                            MBasicBlock &out,
                            uint16_t &nextVRegId) {
    const uint16_t litPtrV = allocateNextVReg(nextVRegId);
    out.instrs.push_back(
        MInstr{MOpcode::AdrPage,
               {MOperand::vregOp(RegClass::GPR, litPtrV), MOperand::labelOp(sym)}});
    out.instrs.push_back(MInstr{MOpcode::AddPageOff,
                                {MOperand::vregOp(RegClass::GPR, litPtrV),
                                 MOperand::vregOp(RegClass::GPR, litPtrV),
                                 MOperand::labelOp(sym)}});
    out.instrs.push_back(MInstr{
        MOpcode::MovRR,
        {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, litPtrV)}});

    if (const auto len = lookupStringLiteralByteLen(sym, stringLiteralByteLengths)) {
        const uint16_t lenV = allocateNextVReg(nextVRegId);
        out.instrs.push_back(MInstr{
            MOpcode::MovRI,
            {MOperand::vregOp(RegClass::GPR, lenV), MOperand::immOp(static_cast<long long>(*len))}});
        out.instrs.push_back(MInstr{
            MOpcode::MovRR,
            {MOperand::regOp(PhysReg::X1), MOperand::vregOp(RegClass::GPR, lenV)}});
        out.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_str_from_lit")}});
        return;
    }

    out.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_const_cstr")}});
}

uint16_t emitConstStrGlobalToVReg(
    const std::string &sym,
    const std::unordered_map<std::string, std::size_t> *stringLiteralByteLengths,
    MBasicBlock &out,
    uint16_t &nextVRegId) {
    emitConstStrGlobalToX0(sym, stringLiteralByteLengths, out, nextVRegId);
    const uint16_t dst = allocateNextVReg(nextVRegId);
    out.instrs.push_back(
        MInstr{MOpcode::MovRR,
               {MOperand::vregOp(RegClass::GPR, dst), MOperand::regOp(PhysReg::X0)}});
    return dst;
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
                            std::unordered_map<unsigned, RegClass> &tempRegClass,
                            uint16_t &nextVRegId,
                            uint16_t &outVReg,
                            RegClass &outCls,
                            const std::unordered_map<std::string, std::size_t>
                                *stringLiteralByteLengths) {
    using Opcode = il::core::Opcode;

    if (v.kind == il::core::Value::Kind::ConstInt) {
        outVReg = allocateNextVReg(nextVRegId);
        outCls = RegClass::GPR;
        out.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::vregOp(outCls, outVReg), MOperand::immOp(v.i64)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::ConstFloat) {
        // Materialize FP constant by moving its bit-pattern via a GPR into an FPR.
        long long bits;
        static_assert(sizeof(double) == sizeof(long long), "size");
        std::memcpy(&bits, &v.f64, sizeof(double));
        const uint16_t tmpG = allocateNextVReg(nextVRegId);
        // Load 64-bit pattern into a GPR vreg
        out.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, tmpG), MOperand::immOp(bits)}});
        outVReg = allocateNextVReg(nextVRegId);
        outCls = RegClass::FPR;
        // fmov dV, xTmp  (bit-cast)
        out.instrs.push_back(MInstr{
            MOpcode::FMovRR,
            {MOperand::vregOp(RegClass::FPR, outVReg), MOperand::vregOp(RegClass::GPR, tmpG)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::NullPtr) {
        // Null pointer is just immediate 0
        outVReg = allocateNextVReg(nextVRegId);
        outCls = RegClass::GPR;
        out.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::vregOp(outCls, outVReg), MOperand::immOp(0)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::GlobalAddr) {
        // Direct GlobalAddr (function pointer or global symbol address)
        // Materialize via PC-relative AdrPage + AddPageOff
        outVReg = allocateNextVReg(nextVRegId);
        outCls = RegClass::GPR;
        const std::string &sym = v.str;
        out.instrs.push_back(MInstr{
            MOpcode::AdrPage, {MOperand::vregOp(RegClass::GPR, outVReg), MOperand::labelOp(sym)}});
        out.instrs.push_back(MInstr{MOpcode::AddPageOff,
                                    {MOperand::vregOp(RegClass::GPR, outVReg),
                                     MOperand::vregOp(RegClass::GPR, outVReg),
                                     MOperand::labelOp(sym)}});
        return true;
    }
    if (v.kind == il::core::Value::Kind::Temp) {
        // First check if we already materialized this temp (includes block params
        // loaded from spill slots in non-entry blocks)
        auto it = tempVReg.find(v.id);
        if (it != tempVReg.end()) {
            outVReg = it->second;
            // Look up register class for this temp
            auto clsIt = tempRegClass.find(v.id);
            outCls = (clsIt != tempRegClass.end()) ? clsIt->second : RegClass::GPR;
            return true;
        }
        // Check if this is an alloca temp - if so, compute its stack address
        // This must be checked before the instruction search since allocas are
        // defined in the entry block but used in other blocks.
        // Note: We don't cache the result in tempVReg because the vreg->phys mapping
        // changes across blocks, and we need to recompute the address each time.
        const int allocaOff = fb.localOffset(v.id);
        if (allocaOff != 0) {
            outVReg = allocateNextVReg(nextVRegId);
            outCls = RegClass::GPR;
            out.instrs.push_back(
                MInstr{MOpcode::AddFpImm,
                       {MOperand::vregOp(RegClass::GPR, outVReg), MOperand::immOp(allocaOff)}});
            // Don't cache: tempVReg[v.id] = outVReg;
            return true;
        }
        // If it's a function entry param (in entry block), move from ABI phys -> vreg.
        // This only applies to entry block parameters, not block parameters in other blocks.
        int pIdx = indexOfParam(bb, v.id);
        if (pIdx >= 0 && pIdx < static_cast<int>(bb.params.size())) {
            // Determine param type
            RegClass cls = RegClass::GPR;
            if (bb.params[static_cast<std::size_t>(pIdx)].type.kind == il::core::Type::Kind::F64) {
                cls = RegClass::FPR;
            }

            // AAPCS64: GPR and FPR arguments use independent register sequences.
            // Count how many args of the same class precede this parameter.
            int classIdx = 0;
            for (int i = 0; i < pIdx; ++i) {
                const bool paramIsFP =
                    bb.params[static_cast<std::size_t>(i)].type.kind == il::core::Type::Kind::F64;
                if ((cls == RegClass::FPR) == paramIsFP)
                    ++classIdx;
            }

            const auto &argOrder = (cls == RegClass::FPR) ? ti.f64ArgOrder : ti.intArgOrder;
            if (classIdx >= static_cast<int>(argOrder.size())) {
                // Stack parameter: compute the caller arg offset.
                // After prologue, caller's stack args are at [FP + 16 + stackArgIdx * 8].
                // Count how many parameters before this one also overflow to stack.
                int stackArgIdx = 0;
                int gprCount = 0;
                int fprCount = 0;
                for (int i = 0; i < pIdx; ++i) {
                    const bool pIsFP = bb.params[static_cast<std::size_t>(i)].type.kind ==
                                       il::core::Type::Kind::F64;
                    if (pIsFP) {
                        if (fprCount < static_cast<int>(ti.f64ArgOrder.size()))
                            ++fprCount;
                        else
                            ++stackArgIdx;
                    } else {
                        if (gprCount < static_cast<int>(ti.intArgOrder.size()))
                            ++gprCount;
                        else
                            ++stackArgIdx;
                    }
                }
                const int callerArgOffset = 16 + stackArgIdx * 8;
                outVReg = allocateNextVReg(nextVRegId);
                outCls = cls;
                if (cls == RegClass::FPR) {
                    out.instrs.push_back(
                        MInstr{MOpcode::LdrFprFpImm,
                               {MOperand::vregOp(cls, outVReg), MOperand::immOp(callerArgOffset)}});
                } else {
                    out.instrs.push_back(
                        MInstr{MOpcode::LdrRegFpImm,
                               {MOperand::vregOp(cls, outVReg), MOperand::immOp(callerArgOffset)}});
                    // Mask i1 parameters to ensure upper bits are zero (stack path).
                    if (bb.params[static_cast<std::size_t>(pIdx)].type.kind ==
                        il::core::Type::Kind::I1)
                        outVReg = emitMaskedI1Value(out, outVReg, nextVRegId);
                }
                return true;
            }

            outVReg = allocateNextVReg(nextVRegId);
            outCls = cls;
            const PhysReg src = argOrder[static_cast<std::size_t>(classIdx)];
            if (cls == RegClass::GPR) {
                out.instrs.push_back(
                    MInstr{MOpcode::MovRR, {MOperand::vregOp(cls, outVReg), MOperand::regOp(src)}});
                // Mask i1 parameters to ensure upper bits are zero (register path).
                if (bb.params[static_cast<std::size_t>(pIdx)].type.kind == il::core::Type::Kind::I1)
                    outVReg = emitMaskedI1Value(out, outVReg, nextVRegId);
            } else {
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

        auto emitRRR =
            [&](MOpcode opc, const il::core::Value &a, const il::core::Value &b) -> bool {
            uint16_t va = 0, vb = 0;
            RegClass ca = RegClass::GPR, cb = RegClass::GPR;
            if (!materializeValueToVReg(
                    a, bb, ti, fb, out, tempVReg, tempRegClass, nextVRegId, va, ca))
                return false;
            if (!materializeValueToVReg(
                    b, bb, ti, fb, out, tempVReg, tempRegClass, nextVRegId, vb, cb))
                return false;
            outVReg = allocateNextVReg(nextVRegId);
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
        auto emitRImm = [&](MOpcode opc, const il::core::Value &a, long long imm) -> bool {
            uint16_t va = 0;
            RegClass ca = RegClass::GPR;
            if (!materializeValueToVReg(
                    a, bb, ti, fb, out, tempVReg, tempRegClass, nextVRegId, va, ca))
                return false;
            outVReg = allocateNextVReg(nextVRegId);
            outCls = (opc == MOpcode::FAddRRR || opc == MOpcode::FSubRRR ||
                      opc == MOpcode::FMulRRR || opc == MOpcode::FDivRRR)
                         ? RegClass::FPR
                         : RegClass::GPR;
            if (opc == MOpcode::AddRI || opc == MOpcode::SubRI || opc == MOpcode::AddOvfRI ||
                opc == MOpcode::SubOvfRI) {
                emitLegalizedSignedImmArith(
                    out,
                    MOperand::vregOp(outCls, outVReg),
                    MOperand::vregOp(outCls, va),
                    imm,
                    (opc == MOpcode::AddRI || opc == MOpcode::AddOvfRI) ? SignedImmArithKind::Add
                                                                        : SignedImmArithKind::Sub,
                    (opc == MOpcode::AddOvfRI || opc == MOpcode::SubOvfRI) ? MOpcode::AddOvfRI
                                                                           : MOpcode::AddRI,
                    (opc == MOpcode::AddOvfRI || opc == MOpcode::SubOvfRI) ? MOpcode::SubOvfRI
                                                                           : MOpcode::SubRI,
                    (opc == MOpcode::AddOvfRI) ? MOpcode::AddOvfRRR : MOpcode::AddRRR,
                    (opc == MOpcode::SubOvfRI) ? MOpcode::SubOvfRRR : MOpcode::SubRRR,
                    [&](long long materializedImm) {
                        const uint16_t tmp = allocateNextVReg(nextVRegId);
                        out.instrs.push_back(MInstr{MOpcode::MovRI,
                                                    {MOperand::vregOp(RegClass::GPR, tmp),
                                                     MOperand::immOp(materializedImm)}});
                        return MOperand::vregOp(RegClass::GPR, tmp);
                    });
                return true;
            }
            out.instrs.push_back(MInstr{opc,
                                        {MOperand::vregOp(outCls, outVReg),
                                         MOperand::vregOp(outCls, va),
                                         MOperand::immOp(imm)}});
            return true;
        };

        const auto &prod = *prodIt;

        // Check for binary operations first using table lookup
        if (const auto *binOp = lookupBinaryOp(prod.op)) {
            if (prod.operands.size() == 2) {
                const bool isImmCandidate =
                    binOp->supportsImmediate &&
                    prod.operands[1].kind == il::core::Value::Kind::ConstInt;
                // For bitwise ops, validate that the constant is a logical immediate.
                const bool isBitwiseImm =
                    isImmCandidate &&
                    (prod.op == il::core::Opcode::And || prod.op == il::core::Opcode::Or ||
                     prod.op == il::core::Opcode::Xor) &&
                    isLogicalImmediate(static_cast<uint64_t>(prod.operands[1].i64));
                // Shift and add/sub immediates are valid if the opcode supports them.
                const bool isOtherImm = isImmCandidate && prod.op != il::core::Opcode::And &&
                                        prod.op != il::core::Opcode::Or &&
                                        prod.op != il::core::Opcode::Xor;
                if (isBitwiseImm || isOtherImm) {
                    if (emitRImm(binOp->immOp, prod.operands[0], prod.operands[1].i64)) {
                        // Cache result to prevent re-materialization with different vreg
                        tempVReg[v.id] = outVReg;
                        return true;
                    }
                    return false;
                } else {
                    // Use register-register form (includes shifts with register amount)
                    if (emitRRR(binOp->mirOp, prod.operands[0], prod.operands[1])) {
                        // Cache result to prevent re-materialization with different vreg
                        tempVReg[v.id] = outVReg;
                        return true;
                    }
                    return false;
                }
            }
        }

        // Handle other operations
        switch (prod.op) {
            case Opcode::ConstStr:
                if (!prod.operands.empty() &&
                    prod.operands[0].kind == il::core::Value::Kind::GlobalAddr) {
                    const std::string &sym = prod.operands[0].str;
                    outVReg =
                        emitConstStrGlobalToVReg(sym, stringLiteralByteLengths, out, nextVRegId);
                    outCls = RegClass::GPR;
                    // Cache for reuse
                    tempVReg[v.id] = outVReg;
                    return true;
                }
                break;
            case Opcode::AddrOf:
                if (!prod.operands.empty() &&
                    prod.operands[0].kind == il::core::Value::Kind::GlobalAddr) {
                    outVReg = allocateNextVReg(nextVRegId);
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
                // AddrOf of a local alloca: compute FP + frame_offset
                if (!prod.operands.empty() &&
                    prod.operands[0].kind == il::core::Value::Kind::Temp) {
                    const int offset = fb.localOffset(prod.operands[0].id);
                    if (offset != 0) {
                        outVReg = allocateNextVReg(nextVRegId);
                        outCls = RegClass::GPR;
                        out.instrs.push_back(MInstr{
                            MOpcode::AddFpImm,
                            {MOperand::vregOp(RegClass::GPR, outVReg), MOperand::immOp(offset)}});
                        tempVReg[v.id] = outVReg;
                        return true;
                    }
                }
                break;
            case Opcode::GEP:
                if (prod.operands.size() >= 2) {
                    uint16_t vbase = 0, voff = 0;
                    RegClass cbase = RegClass::GPR, coff = RegClass::GPR;
                    if (!materializeValueToVReg(prod.operands[0],
                                                bb,
                                                ti,
                                                fb,
                                                out,
                                                tempVReg,
                                                tempRegClass,
                                                nextVRegId,
                                                vbase,
                                                cbase))
                        return false;
                    outVReg = allocateNextVReg(nextVRegId);
                    outCls = RegClass::GPR;
                    const auto &offVal = prod.operands[1];
                    if (offVal.kind == il::core::Value::Kind::ConstInt) {
                        const long long imm = offVal.i64;
                        if (imm == 0) {
                            out.instrs.push_back(MInstr{MOpcode::MovRR,
                                                        {MOperand::vregOp(RegClass::GPR, outVReg),
                                                         MOperand::vregOp(RegClass::GPR, vbase)}});
                        } else {
                            emitLegalizedSignedImmArith(
                                out,
                                MOperand::vregOp(RegClass::GPR, outVReg),
                                MOperand::vregOp(RegClass::GPR, vbase),
                                imm,
                                SignedImmArithKind::Add,
                                MOpcode::AddRI,
                                MOpcode::SubRI,
                                MOpcode::AddRRR,
                                MOpcode::SubRRR,
                                [&](long long materializedImm) {
                                    const uint16_t tmp = allocateNextVReg(nextVRegId);
                                    out.instrs.push_back(
                                        MInstr{MOpcode::MovRI,
                                               {MOperand::vregOp(RegClass::GPR, tmp),
                                                MOperand::immOp(materializedImm)}});
                                    return MOperand::vregOp(RegClass::GPR, tmp);
                                });
                        }
                    } else {
                        if (!materializeValueToVReg(offVal,
                                                    bb,
                                                    ti,
                                                    fb,
                                                    out,
                                                    tempVReg,
                                                    tempRegClass,
                                                    nextVRegId,
                                                    voff,
                                                    coff))
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
                if (isCompareOp(prod.op)) {
                    if (prod.operands.size() == 2) {
                        uint16_t va = 0, vb = 0;
                        RegClass ca = RegClass::GPR, cb = RegClass::GPR;
                        if (!materializeValueToVReg(prod.operands[0],
                                                    bb,
                                                    ti,
                                                    fb,
                                                    out,
                                                    tempVReg,
                                                    tempRegClass,
                                                    nextVRegId,
                                                    va,
                                                    ca))
                            return false;
                        if (!materializeValueToVReg(prod.operands[1],
                                                    bb,
                                                    ti,
                                                    fb,
                                                    out,
                                                    tempVReg,
                                                    tempRegClass,
                                                    nextVRegId,
                                                    vb,
                                                    cb))
                            return false;
                        out.instrs.push_back(MInstr{MOpcode::CmpRR,
                                                    {MOperand::vregOp(RegClass::GPR, va),
                                                     MOperand::vregOp(RegClass::GPR, vb)}});
                        outVReg = allocateNextVReg(nextVRegId);
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
                if (!prod.operands.empty() &&
                    prod.operands[0].kind == il::core::Value::Kind::Temp) {
                    const unsigned allocaId = prod.operands[0].id;
                    const int off = fb.localOffset(allocaId);
                    if (off != 0) {
                        outVReg = allocateNextVReg(nextVRegId);
                        outCls = RegClass::GPR;
                        out.instrs.push_back(
                            MInstr{MOpcode::LdrRegFpImm,
                                   {MOperand::vregOp(outCls, outVReg), MOperand::immOp(off)}});
                        if (prod.type.kind == il::core::Type::Kind::Str)
                            retainStringVReg(out, outVReg);
                        if (prod.type.kind == il::core::Type::Kind::I1)
                            outVReg = emitMaskedI1Value(out, outVReg, nextVRegId);
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
                       std::unordered_map<unsigned, RegClass> &tempRegClass,
                       uint16_t &nextVRegId) {
    // Callee can be in either callI.callee field or operands[0] as GlobalAddr
    std::string callee;
    std::size_t argStart = 0;

    if (!callI.callee.empty()) {
        // Modern IL convention: callee in dedicated field, all operands are arguments
        callee = callI.callee;
        argStart = 0;
    } else if (!callI.operands.empty() &&
               callI.operands[0].kind == il::core::Value::Kind::GlobalAddr) {
        // Legacy convention: callee as GlobalAddr in operands[0]
        callee = callI.operands[0].str;
        argStart = 1;
    } else {
        return false;
    }

    // Map canonical runtime names (e.g., "Viper.Grid2D.New") to C symbols (e.g., "rt_grid2d_new")
    std::string mappedCallee = callee;
    if (auto mapped = il::runtime::mapCanonicalRuntimeName(callee))
        mappedCallee = std::string(*mapped);

    seq.call = MInstr{MOpcode::Bl, {MOperand::labelOp(mappedCallee)}};

    // Detect variadic callee — AAPCS64 requires anonymous (variadic) args on stack.
    const bool isVarArg =
        il::runtime::isVarArgCallee(mappedCallee) || il::runtime::isVarArgCallee(callee);
    std::size_t namedArgCount = SIZE_MAX; // default: all args are "named"
    if (isVarArg) {
        // Look up the function's signature to determine the named parameter count.
        // Variadic args (beyond namedArgCount) must go on stack per AAPCS64.
        if (const auto *sig = il::runtime::findRuntimeSignature(mappedCallee))
            namedArgCount = sig->paramTypes.size();
        else if (const auto *sig2 = il::runtime::findRuntimeSignature(callee))
            namedArgCount = sig2->paramTypes.size();
        else
            namedArgCount = 0; // Conservative: all args to stack for unknown vararg callees
    }

    // First pass: materialize all arguments and collect them
    struct ArgInfo {
        uint16_t vreg;
        viper::codegen::common::CallArgClass cls;
    };

    std::vector<ArgInfo> args;
    args.reserve(callI.operands.size() - argStart);
    for (std::size_t i = argStart; i < callI.operands.size(); ++i) {
        const auto &arg = callI.operands[i];
        uint16_t vr = 0;
        RegClass cls = RegClass::GPR;
        if (!materializeValueToVReg(
                arg, bb, ti, fb, out, tempVReg, tempRegClass, nextVRegId, vr, cls))
            return false;
        args.push_back({vr,
                        cls == RegClass::FPR ? viper::codegen::common::CallArgClass::FPR
                                             : viper::codegen::common::CallArgClass::GPR});
    }

    std::vector<MaterializedCallArg> materialized;
    materialized.reserve(args.size());
    for (const auto &arg : args)
        materialized.push_back({arg.vreg, arg.cls});

    return marshalCallArgs(materialized, namedArgCount, isVarArg, ti, seq);
}

//===----------------------------------------------------------------------===//
// Division/Remainder with Divide-by-Zero Check (parameterized)
//===----------------------------------------------------------------------===//

/// @brief Shared implementation for division and remainder with divide-by-zero check.
/// @details Generates: materialize lhs/rhs, cmp rhs #0, b.eq trap, div/rem, trap block.
///          The four public handlers (lowerSRemChk0, lowerSDivChk0, lowerUDivChk0,
///          lowerURemChk0) are thin wrappers over this single implementation.
/// @param ins       The IL instruction to lower.
/// @param bb        The IL basic block containing @p ins.
/// @param ctx       Lowering context for register allocation and frame state.
/// @param out       The output MIR basic block receiving the lowered sequence.
/// @param isSigned  True for signed (sdiv), false for unsigned (udiv).
/// @param isRemainder True to compute remainder via quotient + msub, false for quotient only.
/// @return True on success, false on lowering failure.
static bool lowerDivisionChk0(const il::core::Instr &ins,
                              const il::core::BasicBlock &bb,
                              LoweringContext &ctx,
                              MBasicBlock &out,
                              bool isSigned,
                              bool isRemainder) {
    if (!ins.result || ins.operands.size() < 2)
        return false;

    // Materialize lhs and rhs
    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::GPR, rhsCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                lhs,
                                lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                rhs,
                                rhsCls))
        return false;

    // Generate divide-by-zero check: cmp rhs, #0; b.eq trap_label
    const std::string trapLabel = ".Ltrap_div0_" + std::to_string(ctx.trapLabelCounter++);
    out.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, rhs), MOperand::immOp(0)}});
    out.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(trapLabel)}});

    // Select division opcode based on signedness.
    const MOpcode divOp = isSigned ? MOpcode::SDivRRR : MOpcode::UDivRRR;

    if (isRemainder) {
        // Compute quotient: [su]div tmp, lhs, rhs
        const uint16_t quotient = allocateNextVReg(ctx.nextVRegId);
        out.instrs.push_back(MInstr{divOp,
                                    {MOperand::vregOp(RegClass::GPR, quotient),
                                     MOperand::vregOp(RegClass::GPR, lhs),
                                     MOperand::vregOp(RegClass::GPR, rhs)}});

        // Compute remainder: msub dst, quotient, rhs, lhs => dst = lhs - quotient*rhs
        const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
        ctx.tempVReg[*ins.result] = dst;
        out.instrs.push_back(MInstr{MOpcode::MSubRRRR,
                                    {MOperand::vregOp(RegClass::GPR, dst),
                                     MOperand::vregOp(RegClass::GPR, quotient),
                                     MOperand::vregOp(RegClass::GPR, rhs),
                                     MOperand::vregOp(RegClass::GPR, lhs)}});
    } else {
        // Compute division directly: [su]div dst, lhs, rhs
        const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
        ctx.tempVReg[*ins.result] = dst;
        out.instrs.push_back(MInstr{divOp,
                                    {MOperand::vregOp(RegClass::GPR, dst),
                                     MOperand::vregOp(RegClass::GPR, lhs),
                                     MOperand::vregOp(RegClass::GPR, rhs)}});
    }

    // Create trap block AFTER all uses of `out` — emplace_back may reallocate
    // the blocks vector, invalidating the `out` reference.
    ctx.mf.blocks.emplace_back();
    ctx.mf.blocks.back().name = trapLabel;
    ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});

    return true;
}

bool lowerSRemChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out) {
    return lowerDivisionChk0(ins, bb, ctx, out, /*isSigned=*/true, /*isRemainder=*/true);
}

bool lowerSDivChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out) {
    return lowerDivisionChk0(ins, bb, ctx, out, /*isSigned=*/true, /*isRemainder=*/false);
}

bool lowerUDivChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out) {
    return lowerDivisionChk0(ins, bb, ctx, out, /*isSigned=*/false, /*isRemainder=*/false);
}

bool lowerURemChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out) {
    return lowerDivisionChk0(ins, bb, ctx, out, /*isSigned=*/false, /*isRemainder=*/true);
}

//===----------------------------------------------------------------------===//
// Index Bounds Check (idx.chk)
//===----------------------------------------------------------------------===//

bool lowerIdxChk(const il::core::Instr &ins,
                 const il::core::BasicBlock &bb,
                 LoweringContext &ctx,
                 MBasicBlock &out) {
    // idx.chk checks: lo <= idx < hi
    // operands[0] = idx, operands[1] = lo, operands[2] = hi
    if (!ins.result || ins.operands.size() < 3)
        return false;

    uint16_t idxV = 0, loV = 0, hiV = 0;
    RegClass idxCls = RegClass::GPR, loCls = RegClass::GPR, hiCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                idxV,
                                idxCls))
        return false;

    // Check if lo is constant 0 (common optimization)
    const bool loIsZero =
        ins.operands[1].kind == il::core::Value::Kind::ConstInt && ins.operands[1].i64 == 0;

    if (!loIsZero) {
        if (!materializeValueToVReg(ins.operands[1],
                                    bb,
                                    ctx.ti,
                                    ctx.fb,
                                    out,
                                    ctx.tempVReg,
                                    ctx.tempRegClass,
                                    ctx.nextVRegId,
                                    loV,
                                    loCls))
            return false;
    }

    if (!materializeValueToVReg(ins.operands[2],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                hiV,
                                hiCls))
        return false;

    const std::string trapLabel = ".Ltrap_bounds_" + std::to_string(ctx.trapLabelCounter++);

    if (loIsZero) {
        // Optimized case: just check idx >= hi (unsigned)
        // cmp idx, hi; b.hs trap (unsigned >=)
        out.instrs.push_back(
            MInstr{MOpcode::CmpRR,
                   {MOperand::vregOp(RegClass::GPR, idxV), MOperand::vregOp(RegClass::GPR, hiV)}});
        out.instrs.push_back(
            MInstr{MOpcode::BCond, {MOperand::condOp("hs"), MOperand::labelOp(trapLabel)}});
    } else {
        // General case: check idx < lo OR idx >= hi
        // cmp idx, lo; b.lt trap
        out.instrs.push_back(
            MInstr{MOpcode::CmpRR,
                   {MOperand::vregOp(RegClass::GPR, idxV), MOperand::vregOp(RegClass::GPR, loV)}});
        out.instrs.push_back(
            MInstr{MOpcode::BCond, {MOperand::condOp("lt"), MOperand::labelOp(trapLabel)}});

        // cmp idx, hi; b.ge trap
        out.instrs.push_back(
            MInstr{MOpcode::CmpRR,
                   {MOperand::vregOp(RegClass::GPR, idxV), MOperand::vregOp(RegClass::GPR, hiV)}});
        out.instrs.push_back(
            MInstr{MOpcode::BCond, {MOperand::condOp("ge"), MOperand::labelOp(trapLabel)}});
    }

    // Result is the index value (pass-through)
    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    out.instrs.push_back(
        MInstr{MOpcode::MovRR,
               {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::GPR, idxV)}});

    // Create trap block AFTER all uses of `out` — emplace_back may reallocate
    // the blocks vector, invalidating the `out` reference.
    ctx.mf.blocks.emplace_back();
    ctx.mf.blocks.back().name = trapLabel;
    ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});

    return true;
}

//===----------------------------------------------------------------------===//
// Signed Remainder (srem) - no zero-check
//===----------------------------------------------------------------------===//

bool lowerSRem(const il::core::Instr &ins,
               const il::core::BasicBlock &bb,
               LoweringContext &ctx,
               MBasicBlock &out) {
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::GPR, rhsCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                lhs,
                                lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                rhs,
                                rhsCls))
        return false;

    // Compute quotient: sdiv tmp, lhs, rhs
    const uint16_t quotient = allocateNextVReg(ctx.nextVRegId);
    out.instrs.push_back(MInstr{MOpcode::SDivRRR,
                                {MOperand::vregOp(RegClass::GPR, quotient),
                                 MOperand::vregOp(RegClass::GPR, lhs),
                                 MOperand::vregOp(RegClass::GPR, rhs)}});

    // Compute remainder: msub dst, quotient, rhs, lhs => dst = lhs - quotient*rhs
    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    out.instrs.push_back(MInstr{MOpcode::MSubRRRR,
                                {MOperand::vregOp(RegClass::GPR, dst),
                                 MOperand::vregOp(RegClass::GPR, quotient),
                                 MOperand::vregOp(RegClass::GPR, rhs),
                                 MOperand::vregOp(RegClass::GPR, lhs)}});

    return true;
}

//===----------------------------------------------------------------------===//
// Unsigned Remainder (urem) - no zero-check
//===----------------------------------------------------------------------===//

bool lowerURem(const il::core::Instr &ins,
               const il::core::BasicBlock &bb,
               LoweringContext &ctx,
               MBasicBlock &out) {
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::GPR, rhsCls = RegClass::GPR;

    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                lhs,
                                lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                rhs,
                                rhsCls))
        return false;

    // Compute quotient: udiv tmp, lhs, rhs
    const uint16_t quotient = allocateNextVReg(ctx.nextVRegId);
    out.instrs.push_back(MInstr{MOpcode::UDivRRR,
                                {MOperand::vregOp(RegClass::GPR, quotient),
                                 MOperand::vregOp(RegClass::GPR, lhs),
                                 MOperand::vregOp(RegClass::GPR, rhs)}});

    // Compute remainder: msub dst, quotient, rhs, lhs => dst = lhs - quotient*rhs
    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
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

static uint16_t ensureFpr(uint16_t vreg, RegClass &cls, LoweringContext &ctx, MBasicBlock &out) {
    if (cls != RegClass::GPR)
        return vreg;

    const uint16_t converted = allocateNextVReg(ctx.nextVRegId);
    out.instrs.push_back(MInstr{
        MOpcode::SCvtF,
        {MOperand::vregOp(RegClass::FPR, converted), MOperand::vregOp(RegClass::GPR, vreg)}});
    cls = RegClass::FPR;
    return converted;
}

bool lowerFpArithmetic(const il::core::Instr &ins,
                       const il::core::BasicBlock &bb,
                       LoweringContext &ctx,
                       MBasicBlock &out) {
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::FPR, rhsCls = RegClass::FPR;

    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                lhs,
                                lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                rhs,
                                rhsCls))
        return false;

    // BUG-007 fix: If operands are GPR (integer constants), convert them to FPR.
    // This handles cases like `fmul %t4, 2` where the literal 2 is an integer.
    lhs = ensureFpr(lhs, lhsCls, ctx, out);
    rhs = ensureFpr(rhs, rhsCls, ctx, out);

    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    ctx.tempRegClass[*ins.result] = RegClass::FPR;

    MOpcode mop = MOpcode::FAddRRR;
    switch (ins.op) {
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
                    MBasicBlock &out) {
    if (!ins.result || ins.operands.size() < 2)
        return false;

    uint16_t lhs = 0, rhs = 0;
    RegClass lhsCls = RegClass::FPR, rhsCls = RegClass::FPR;

    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                lhs,
                                lhsCls))
        return false;
    if (!materializeValueToVReg(ins.operands[1],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                rhs,
                                rhsCls))
        return false;

    // BUG-007 fix: If operands are GPR (integer constants), convert them to FPR.
    lhs = ensureFpr(lhs, lhsCls, ctx, out);
    rhs = ensureFpr(rhs, rhsCls, ctx, out);

    // Emit fcmp
    out.instrs.push_back(
        MInstr{MOpcode::FCmpRR,
               {MOperand::vregOp(RegClass::FPR, lhs), MOperand::vregOp(RegClass::FPR, rhs)}});

    // Emit cset with appropriate condition
    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    ctx.tempRegClass[*ins.result] = RegClass::GPR;
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
                 MBasicBlock &out) {
    if (!ins.result || ins.operands.empty())
        return false;

    uint16_t sv = 0;
    RegClass scls = RegClass::GPR;
    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                sv,
                                scls))
        return false;

    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    ctx.tempRegClass[*ins.result] = RegClass::FPR;

    out.instrs.push_back(
        MInstr{MOpcode::SCvtF,
               {MOperand::vregOp(RegClass::FPR, dst), MOperand::vregOp(RegClass::GPR, sv)}});
    return true;
}

//===----------------------------------------------------------------------===//
// fptosi (float to signed int)
//===----------------------------------------------------------------------===//

bool lowerFptosi(const il::core::Instr &ins,
                 const il::core::BasicBlock &bb,
                 LoweringContext &ctx,
                 MBasicBlock &out) {
    if (!ins.result || ins.operands.empty())
        return false;

    uint16_t fv = 0;
    RegClass fcls = RegClass::FPR;
    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                fv,
                                fcls))
        return false;

    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    ctx.tempRegClass[*ins.result] = RegClass::GPR;

    out.instrs.push_back(
        MInstr{MOpcode::FCvtZS,
               {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::FPR, fv)}});
    return true;
}

//===----------------------------------------------------------------------===//
// Zext1/Trunc1 (Boolean conversion)
//===----------------------------------------------------------------------===//

bool lowerZext1Trunc1(const il::core::Instr &ins,
                      const il::core::BasicBlock &bb,
                      LoweringContext &ctx,
                      MBasicBlock &out) {
    if (!ins.result || ins.operands.empty())
        return false;

    uint16_t sv = 0;
    RegClass scls = RegClass::GPR;
    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                sv,
                                scls))
        return false;

    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    ctx.tempRegClass[*ins.result] = RegClass::GPR;

    // dst = sv & 1
    const uint16_t one = allocateNextVReg(ctx.nextVRegId);
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
                        MBasicBlock &out) {
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
    if (!materializeValueToVReg(ins.operands[0],
                                bb,
                                ctx.ti,
                                ctx.fb,
                                out,
                                ctx.tempVReg,
                                ctx.tempRegClass,
                                ctx.nextVRegId,
                                sv,
                                scls))
        return false;

    const uint16_t vt = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = vt;

    // vt = narrowed version of sv
    if (sh > 0) {
        // Copy sv into vt first
        out.instrs.push_back(
            MInstr{MOpcode::MovRR,
                   {MOperand::vregOp(RegClass::GPR, vt), MOperand::vregOp(RegClass::GPR, sv)}});
        if (ins.op == il::core::Opcode::CastSiNarrowChk) {
            out.instrs.push_back(MInstr{MOpcode::LslRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
            out.instrs.push_back(MInstr{MOpcode::AsrRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
        } else {
            out.instrs.push_back(MInstr{MOpcode::LslRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
            out.instrs.push_back(MInstr{MOpcode::LsrRI,
                                        {MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::vregOp(RegClass::GPR, vt),
                                         MOperand::immOp(sh)}});
        }
    } else {
        // No change in width - just copy
        out.instrs.push_back(
            MInstr{MOpcode::MovRR,
                   {MOperand::vregOp(RegClass::GPR, vt), MOperand::vregOp(RegClass::GPR, sv)}});
    }
    return true;
}

//===----------------------------------------------------------------------===//
// Memory Operations — Store
//===----------------------------------------------------------------------===//

bool lowerStore(const il::core::Instr &ins,
                const il::core::BasicBlock &bb,
                LoweringContext &ctx,
                MBasicBlock &out) {
    if (ins.operands.size() != 2)
        return true;
    if (ins.operands[0].kind != il::core::Value::Kind::Temp)
        return true;
    const unsigned ptrId = ins.operands[0].id;
    const int off = ctx.fb.localOffset(ptrId);
    if (off != 0) {
        // Store to alloca local via FP offset
        uint16_t v = 0;
        RegClass cls = RegClass::GPR;
        if (materializeValueToVReg(ins.operands[1], bb, ctx, out, v, cls)) {
            const bool dstIsFP = (ins.type.kind == il::core::Type::Kind::F64);
            if (dstIsFP) {
                uint16_t srcF = v;
                if (cls != RegClass::FPR) {
                    srcF = allocateNextVReg(ctx.nextVRegId);
                    out.instrs.push_back(MInstr{MOpcode::SCvtF,
                                                {MOperand::vregOp(RegClass::FPR, srcF),
                                                 MOperand::vregOp(RegClass::GPR, v)}});
                }
                out.instrs.push_back(
                    MInstr{MOpcode::StrFprFpImm,
                           {MOperand::vregOp(RegClass::FPR, srcF), MOperand::immOp(off)}});
            } else {
                out.instrs.push_back(
                    MInstr{MOpcode::StrRegFpImm,
                           {MOperand::vregOp(RegClass::GPR, v), MOperand::immOp(off)}});
            }
        }
    } else {
        // General store via base-in-vreg
        uint16_t vbase = 0, vval = 0;
        RegClass cbase = RegClass::GPR, cval = RegClass::GPR;
        if (materializeValueToVReg(ins.operands[0], bb, ctx, out, vbase, cbase) &&
            materializeValueToVReg(ins.operands[1], bb, ctx, out, vval, cval)) {
            const bool dstIsFP = (ins.type.kind == il::core::Type::Kind::F64);
            if (dstIsFP) {
                uint16_t srcF = vval;
                if (cval != RegClass::FPR) {
                    srcF = allocateNextVReg(ctx.nextVRegId);
                    out.instrs.push_back(MInstr{MOpcode::SCvtF,
                                                {MOperand::vregOp(RegClass::FPR, srcF),
                                                 MOperand::vregOp(RegClass::GPR, vval)}});
                }
                out.instrs.push_back(MInstr{MOpcode::StrFprBaseImm,
                                            {MOperand::vregOp(RegClass::FPR, srcF),
                                             MOperand::vregOp(RegClass::GPR, vbase),
                                             MOperand::immOp(0)}});
            } else {
                out.instrs.push_back(MInstr{MOpcode::StrRegBaseImm,
                                            {MOperand::vregOp(RegClass::GPR, vval),
                                             MOperand::vregOp(RegClass::GPR, vbase),
                                             MOperand::immOp(0)}});
            }
        }
    }
    return true;
}

//===----------------------------------------------------------------------===//
// Memory Operations — Load
//===----------------------------------------------------------------------===//

bool lowerLoad(const il::core::Instr &ins,
               const il::core::BasicBlock &bb,
               LoweringContext &ctx,
               MBasicBlock &out) {
    if (!ins.result || ins.operands.empty())
        return true;
    if (ins.operands[0].kind != il::core::Value::Kind::Temp)
        return true;
    const unsigned ptrId = ins.operands[0].id;
    const int off = ctx.fb.localOffset(ptrId);
    const bool isFP = (ins.type.kind == il::core::Type::Kind::F64);
    const bool isBool = (ins.type.kind == il::core::Type::Kind::I1);
    if (off != 0) {
        const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
        if (isFP) {
            ctx.tempRegClass[*ins.result] = RegClass::FPR;
            out.instrs.push_back(
                MInstr{MOpcode::LdrFprFpImm,
                       {MOperand::vregOp(RegClass::FPR, dst), MOperand::immOp(off)}});
            ctx.tempVReg[*ins.result] = dst;
        } else {
            out.instrs.push_back(
                MInstr{MOpcode::LdrRegFpImm,
                       {MOperand::vregOp(RegClass::GPR, dst), MOperand::immOp(off)}});
            if (ins.type.kind == il::core::Type::Kind::Str)
                retainStringVReg(out, dst);
            ctx.tempRegClass[*ins.result] = RegClass::GPR;
            ctx.tempVReg[*ins.result] = isBool ? emitMaskedI1Value(out, dst, ctx.nextVRegId) : dst;
        }
    } else {
        uint16_t vbase = 0;
        RegClass cbase = RegClass::GPR;
        if (materializeValueToVReg(ins.operands[0], bb, ctx, out, vbase, cbase)) {
            const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
            if (isFP) {
                ctx.tempRegClass[*ins.result] = RegClass::FPR;
                out.instrs.push_back(MInstr{MOpcode::LdrFprBaseImm,
                                            {MOperand::vregOp(RegClass::FPR, dst),
                                             MOperand::vregOp(RegClass::GPR, vbase),
                                             MOperand::immOp(0)}});
                ctx.tempVReg[*ins.result] = dst;
            } else {
                out.instrs.push_back(MInstr{MOpcode::LdrRegBaseImm,
                                            {MOperand::vregOp(RegClass::GPR, dst),
                                             MOperand::vregOp(RegClass::GPR, vbase),
                                             MOperand::immOp(0)}});
                if (ins.type.kind == il::core::Type::Kind::Str)
                    retainStringVReg(out, dst);
                ctx.tempRegClass[*ins.result] = RegClass::GPR;
                ctx.tempVReg[*ins.result] =
                    isBool ? emitMaskedI1Value(out, dst, ctx.nextVRegId) : dst;
            }
        }
    }
    return true;
}

//===----------------------------------------------------------------------===//
// Memory Operations — GEP
//===----------------------------------------------------------------------===//

bool lowerGEP(const il::core::Instr &ins,
              const il::core::BasicBlock &bb,
              LoweringContext &ctx,
              MBasicBlock &out) {
    if (!ins.result || ins.operands.size() < 2)
        return true;
    uint16_t vbase = 0;
    RegClass cbase = RegClass::GPR;
    if (!materializeValueToVReg(ins.operands[0], bb, ctx, out, vbase, cbase))
        return true;
    const uint16_t dst = allocateNextVReg(ctx.nextVRegId);
    ctx.tempVReg[*ins.result] = dst;
    const auto &offVal = ins.operands[1];
    if (offVal.kind == il::core::Value::Kind::ConstInt) {
        const long long imm = offVal.i64;
        if (imm == 0) {
            out.instrs.push_back(MInstr{
                MOpcode::MovRR,
                {MOperand::vregOp(RegClass::GPR, dst), MOperand::vregOp(RegClass::GPR, vbase)}});
        } else {
            emitLegalizedSignedImmArith(
                out,
                MOperand::vregOp(RegClass::GPR, dst),
                MOperand::vregOp(RegClass::GPR, vbase),
                imm,
                SignedImmArithKind::Add,
                MOpcode::AddRI,
                MOpcode::SubRI,
                MOpcode::AddRRR,
                MOpcode::SubRRR,
                [&](long long materializedImm) {
                    const uint16_t tmp = allocateNextVReg(ctx.nextVRegId);
                    out.instrs.push_back(MInstr{
                        MOpcode::MovRI,
                        {MOperand::vregOp(RegClass::GPR, tmp), MOperand::immOp(materializedImm)}});
                    return MOperand::vregOp(RegClass::GPR, tmp);
                });
        }
    } else {
        uint16_t voff = 0;
        RegClass coff = RegClass::GPR;
        if (materializeValueToVReg(offVal, bb, ctx, out, voff, coff)) {
            out.instrs.push_back(MInstr{MOpcode::AddRRR,
                                        {MOperand::vregOp(RegClass::GPR, dst),
                                         MOperand::vregOp(RegClass::GPR, vbase),
                                         MOperand::vregOp(RegClass::GPR, voff)}});
        }
    }
    return true;
}

//===----------------------------------------------------------------------===//
// Call & Return — Call
//===----------------------------------------------------------------------===//

bool lowerCall(const il::core::Instr &ins,
               const il::core::BasicBlock &bb,
               LoweringContext &ctx,
               MBasicBlock &out) {
    LoweredCall seq{};
    if (lowerCallWithArgs(
            ins, bb, ctx.ti, ctx.fb, out, seq, ctx.tempVReg, ctx.tempRegClass, ctx.nextVRegId)) {
        for (auto &mi : seq.prefix)
            out.instrs.push_back(std::move(mi));
        out.instrs.push_back(std::move(seq.call));
        for (auto &mi : seq.postfix)
            out.instrs.push_back(std::move(mi));
        if (ins.result) {
            const uint16_t dst =
                captureCallResult(ins, ctx.ti, out, ctx.tempVReg, ctx.tempRegClass, ctx.nextVRegId);
            if (ins.callee == "rt_arr_obj_get") {
                const int off = ctx.fb.ensureSpill(dst);
                out.instrs.push_back(
                    MInstr{MOpcode::StrRegFpImm,
                           {MOperand::vregOp(RegClass::GPR, dst), MOperand::immOp(off)}});
                const uint16_t dst2 = allocateNextVReg(ctx.nextVRegId);
                out.instrs.push_back(
                    MInstr{MOpcode::LdrRegFpImm,
                           {MOperand::vregOp(RegClass::GPR, dst2), MOperand::immOp(off)}});
                ctx.tempVReg[*ins.result] = dst2;
            }
        }
    } else if (!ins.callee.empty()) {
        throw std::runtime_error("AArch64 lowering: failed to lower call arguments for " +
                                 ins.callee);
    }
    return true;
}

//===----------------------------------------------------------------------===//
// Call & Return — CallIndirect
//===----------------------------------------------------------------------===//

bool lowerCallIndirect(const il::core::Instr &ins,
                       const il::core::BasicBlock &bb,
                       LoweringContext &ctx,
                       MBasicBlock &out) {
    if (ins.operands.empty())
        return true;

    uint16_t vFuncPtr = 0;
    RegClass cFuncPtr = RegClass::GPR;
    if (!materializeValueToVReg(ins.operands[0], bb, ctx, out, vFuncPtr, cFuncPtr))
        return true;

    std::vector<MaterializedCallArg> args;
    args.reserve(ins.operands.size() - 1);
    for (std::size_t i = 1; i < ins.operands.size(); ++i) {
        uint16_t vArg = 0;
        RegClass cArg = RegClass::GPR;
        if (!materializeValueToVReg(ins.operands[i], bb, ctx, out, vArg, cArg))
            return true;
        args.push_back({vArg,
                        cArg == RegClass::FPR ? viper::codegen::common::CallArgClass::FPR
                                              : viper::codegen::common::CallArgClass::GPR});
    }

    LoweredCall seq{};
    if (!marshalCallArgs(args, args.size(), false, ctx.ti, seq))
        return false;
    seq.prefix.push_back(MInstr{
        MOpcode::MovRR, {MOperand::regOp(PhysReg::X9), MOperand::vregOp(RegClass::GPR, vFuncPtr)}});
    seq.call = MInstr{MOpcode::Blr, {MOperand::regOp(PhysReg::X9)}};
    for (auto &mi : seq.prefix)
        out.instrs.push_back(std::move(mi));
    out.instrs.push_back(std::move(seq.call));
    for (auto &mi : seq.postfix)
        out.instrs.push_back(std::move(mi));

    if (ins.result)
        captureCallResult(ins, ctx.ti, out, ctx.tempVReg, ctx.tempRegClass, ctx.nextVRegId);
    return true;
}

//===----------------------------------------------------------------------===//
// Call & Return — Ret
//===----------------------------------------------------------------------===//

bool lowerRet(const il::core::Instr &ins,
              const il::core::BasicBlock &bb,
              LoweringContext &ctx,
              MBasicBlock &out) {
    using Opcode = il::core::Opcode;

    if (!ins.operands.empty()) {
        uint16_t v = 0;
        RegClass cls = RegClass::GPR;
        bool ok = materializeValueToVReg(ins.operands[0], bb, ctx, out, v, cls);
        if (!ok && ins.operands[0].kind == il::core::Value::Kind::Temp) {
            const unsigned rid = ins.operands[0].id;
            auto it = std::find_if(
                bb.instructions.begin(), bb.instructions.end(), [&](const il::core::Instr &I) {
                    return I.result && *I.result == rid;
                });
            if (it != bb.instructions.end()) {
                const auto &prod = *it;
                if (!prod.operands.empty() &&
                    prod.operands[0].kind == il::core::Value::Kind::GlobalAddr) {
                    const std::string &sym = prod.operands[0].str;
                    if (prod.op == Opcode::ConstStr) {
                        emitConstStrGlobalToX0(
                            sym, ctx.stringLiteralByteLengths, out, ctx.nextVRegId);
                        v = allocateNextVReg(ctx.nextVRegId);
                        cls = RegClass::GPR;
                        out.instrs.push_back(
                            MInstr{MOpcode::MovRR,
                                   {MOperand::vregOp(RegClass::GPR, v),
                                    MOperand::regOp(PhysReg::X0)}});
                        ctx.tempVReg[rid] = v;
                        ok = true;
                    } else if (prod.op == Opcode::AddrOf) {
                        v = allocateNextVReg(ctx.nextVRegId);
                        cls = RegClass::GPR;
                        out.instrs.push_back(
                            MInstr{MOpcode::AdrPage,
                                   {MOperand::vregOp(RegClass::GPR, v),
                                    MOperand::labelOp(sym)}});
                        out.instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                    {MOperand::vregOp(RegClass::GPR, v),
                                                     MOperand::vregOp(RegClass::GPR, v),
                                                     MOperand::labelOp(sym)}});
                        ctx.tempVReg[rid] = v;
                        ok = true;
                    }
                }
            }
        }
        if (ok) {
            if (cls == RegClass::FPR) {
                out.instrs.push_back(MInstr{
                    MOpcode::FMovRR,
                    {MOperand::regOp(ctx.ti.f64ReturnReg), MOperand::vregOp(RegClass::FPR, v)}});
            } else {
                uint16_t retVReg = v;
                if (ctx.fn.retType.kind == il::core::Type::Kind::I1)
                    retVReg = emitMaskedI1Value(out, v, ctx.nextVRegId);
                out.instrs.push_back(MInstr{
                    MOpcode::MovRR,
                    {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, retVReg)}});
            }
        }
    }
    if (ins.operands.empty() && ctx.mf.name == "main") {
        out.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    }
    out.instrs.push_back(MInstr{MOpcode::Ret, {}});
    return true;
}

} // namespace viper::codegen::aarch64
