//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.Mem.cpp
// Purpose: Implement memory-oriented opcode lowering rules for the provisional
//          IL dialect, including loads, stores, and call sequencing.
// Key invariants: Emitters rely on EmitCommon for operand preparation, preserve
//                 ABI-mandated register classes, and never emit instructions
//                 when operand requirements are unmet.
// Ownership/Lifetime: Operates purely on borrowed MIRBuilder state and records
//                     call metadata for later passes without taking ownership of
//                     IR nodes.
// Links: docs/codemap.md, docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "CallLowering.hpp"
#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"
#include "OperandUtils.hpp"

#include "il/runtime/RuntimeSignatures.hpp"

#include <utility>
#include <vector>

namespace viper::codegen::x64::lowering
{

/// @brief Lower an IL call instruction into the backend call plan.
/// @details Builds a @ref CallLoweringPlan by classifying the callee operand,
///          materialising argument descriptors, and reserving result vregs when
///          present.  The completed plan is recorded on the @p builder so that
///          later lowering phases can emit ABI-conforming prologues and
///          epilogues.  Finally, a placeholder CALL is appended to the Machine
///          IR so scheduling and register allocation see the pending call.
/// @param instr High-level IL instruction describing the call.
/// @param builder MIR construction context that owns register state.
void emitCall(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty())
    {
        return;
    }

    CallLoweringPlan plan{};
    plan.calleeLabel = instr.ops.front().label;
    // Query the runtime signature registry to determine if the callee uses
    // C-style variadic arguments. The utility consults registered signatures
    // first, then falls back to a curated list of known vararg C functions.
    if (!plan.calleeLabel.empty())
    {
        plan.isVarArg = il::runtime::isVarArgCallee(plan.calleeLabel);
    }

    for (std::size_t idx = 1; idx < instr.ops.size(); ++idx)
    {
        const auto &argVal = instr.ops[idx];
        CallArg arg{};
        arg.kind = builder.regClassFor(argVal.kind) == RegClass::GPR ? CallArg::GPR : CallArg::XMM;

        if (builder.isImmediate(argVal))
        {
            arg.isImm = true;
            arg.imm = argVal.i64;
        }
        else
        {
            const Operand operand =
                builder.makeOperandForValue(argVal, builder.regClassFor(argVal.kind));
            if (const auto *reg = std::get_if<OpReg>(&operand))
            {
                arg.vreg = reg->idOrPhys;
            }
            else if (const auto *imm = std::get_if<OpImm>(&operand))
            {
                arg.isImm = true;
                arg.imm = imm->val;
            }
        }

        plan.args.push_back(arg);
    }

    VReg resultVReg{};
    bool hasResult = (instr.resultId >= 0);
    if (hasResult)
    {
        resultVReg = builder.ensureVReg(instr.resultId, instr.resultKind);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            plan.returnsF64 = true;
        }
    }

    builder.recordCallPlan(std::move(plan));
    builder.append(
        MInstr::make(MOpcode::CALL, std::vector<Operand>{builder.makeLabelOperand(instr.ops[0])}));

    // Emit MOV to capture return value from ABI return register to result virtual register
    if (hasResult)
    {
        const Operand resultOp = makeVRegOperand(resultVReg.cls, resultVReg.id);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            // Float return in XMM0
            const Operand retReg = makePhysRegOperand(
                RegClass::XMM, static_cast<uint16_t>(builder.target().f64ReturnReg));
            builder.append(MInstr::make(MOpcode::MOVSDrr, std::vector<Operand>{resultOp, retReg}));
        }
        else
        {
            // Integer/pointer return in RAX
            const Operand retReg = makePhysRegOperand(
                RegClass::GPR, static_cast<uint16_t>(builder.target().intReturnReg));
            builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{resultOp, retReg}));
        }
    }
}

/// @brief Lower an IL call.indirect instruction into the backend call plan.
/// @details Similar to emitCall but treats the first operand as a value holding
///          the callee pointer (in a register or memory). Records the call plan
///          for argument setup and appends a CALL with an indirect target.
/// @param instr High-level IL instruction describing the indirect call.
/// @param builder MIR construction context that owns register state.
void emitCallIndirect(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty())
    {
        return;
    }

    CallLoweringPlan plan{};
    // No label for indirect calls; vararg detection is conservative here.

    for (std::size_t idx = 1; idx < instr.ops.size(); ++idx)
    {
        const auto &argVal = instr.ops[idx];
        CallArg arg{};
        arg.kind = builder.regClassFor(argVal.kind) == RegClass::GPR ? CallArg::GPR : CallArg::XMM;

        if (builder.isImmediate(argVal))
        {
            arg.isImm = true;
            arg.imm = argVal.i64;
        }
        else
        {
            const Operand operand =
                builder.makeOperandForValue(argVal, builder.regClassFor(argVal.kind));
            if (const auto *reg = std::get_if<OpReg>(&operand))
            {
                arg.vreg = reg->idOrPhys;
            }
            else if (const auto *imm = std::get_if<OpImm>(&operand))
            {
                arg.isImm = true;
                arg.imm = imm->val;
            }
        }

        plan.args.push_back(arg);
    }

    VReg resultVReg{};
    bool hasResult = (instr.resultId >= 0);
    if (hasResult)
    {
        resultVReg = builder.ensureVReg(instr.resultId, instr.resultKind);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            plan.returnsF64 = true;
        }
    }

    builder.recordCallPlan(std::move(plan));
    // Use GPR as preferred class when materialising the callee pointer.
    const Operand calleeOp = builder.makeOperandForValue(instr.ops[0], RegClass::GPR);
    builder.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{calleeOp}));

    // Emit MOV to capture return value from ABI return register to result virtual register
    if (hasResult)
    {
        const Operand resultOp = makeVRegOperand(resultVReg.cls, resultVReg.id);
        if (instr.resultKind == ILValue::Kind::F64)
        {
            // Float return in XMM0
            const Operand retReg = makePhysRegOperand(
                RegClass::XMM, static_cast<uint16_t>(builder.target().f64ReturnReg));
            builder.append(MInstr::make(MOpcode::MOVSDrr, std::vector<Operand>{resultOp, retReg}));
        }
        else
        {
            // Integer/pointer return in RAX
            const Operand retReg = makePhysRegOperand(
                RegClass::GPR, static_cast<uint16_t>(builder.target().intReturnReg));
            builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{resultOp, retReg}));
        }
    }
}

/// @brief Lower an automatic storage load instruction.
/// @details Delegates to @ref EmitCommon::emitLoad so that addressing modes and
///          register class selection stay consistent with the rest of the
///          backend.  The helper ensures the result vreg is allocated in the
///          correct class for the instruction's result kind.
/// @param instr IL instruction representing the load.
/// @param builder MIR construction context.
void emitLoadAuto(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitLoad(instr, builder.regClassFor(instr.resultKind));
}

/// @brief Lower a store instruction targeting automatic storage.
/// @details Invokes @ref EmitCommon::emitStore to synthesise the necessary
///          Machine IR operations.  Using the shared helper keeps store
///          semantics aligned with other lowering paths and guarantees
///          consistent operand validation.
/// @param instr IL store instruction.
/// @param builder MIR construction context.
void emitStore(const ILInstr &instr, MIRBuilder &builder)
{
    EmitCommon(builder).emitStore(instr);
}

/// @brief Lower a const_str instruction to produce a runtime string handle.
/// @details Emits a call to rt_str_from_lit with the string literal data,
///          storing the result in the destination vreg.
/// @param instr IL const_str instruction with string operand.
/// @param builder MIR construction context.
void emitConstStr(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.ops.empty() || instr.resultId < 0)
    {
        return;
    }

    // The operand contains the string literal data
    const auto &strVal = instr.ops.front();

    // Reserve the result vreg
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    // Materialize the string using the MIRBuilder's STR handling
    // This emits LEA + CALL rt_str_from_lit and returns the result
    const Operand strOp = builder.makeOperandForValue(strVal, RegClass::GPR);

    // Copy the materialized result to the destination vreg
    builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{dest, strOp}));
}

/// @brief Lower an alloca instruction to allocate stack space.
/// @details Allocates a stack slot and produces the address in the result vreg.
///          The actual frame offset is assigned during FrameLowering pass.
/// @param instr IL alloca instruction with size operand.
/// @param builder MIR construction context.
void emitAlloca(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.resultId < 0)
    {
        return;
    }

    // Reserve the result vreg for the pointer
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    // Get the allocation size (used for frame layout calculation)
    const int64_t size = instr.ops.empty() ? 8 : instr.ops[0].i64;

    // Use a placeholder negative offset that FrameLowering will resolve.
    // The slot index is derived from the result SSA id to ensure uniqueness.
    const int32_t placeholderOffset = -static_cast<int32_t>((instr.resultId + 1) * 8);
    (void)size; // Size is used by frame builder, not needed here

    // LEA dest, [rbp + offset]
    const OpReg rbpBase = makePhysBase(PhysReg::RBP);
    const Operand mem = makeMemOperand(rbpBase, placeholderOffset);
    builder.append(MInstr::make(MOpcode::LEA, std::vector<Operand>{dest, mem}));
}

/// @brief Lower a GEP (get element pointer) instruction.
/// @details Computes base + offset and stores the result pointer.
/// @param instr IL GEP instruction with base and offset operands.
/// @param builder MIR construction context.
void emitGEP(const ILInstr &instr, MIRBuilder &builder)
{
    if (instr.resultId < 0 || instr.ops.size() < 2)
    {
        return;
    }

    // Reserve the result vreg for the pointer
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    // Get the base pointer
    const Operand baseOp = builder.makeOperandForValue(instr.ops[0], RegClass::GPR);
    const auto *baseReg = std::get_if<OpReg>(&baseOp);

    // Get the offset
    const auto &offsetVal = instr.ops[1];

    if (baseReg && builder.isImmediate(offsetVal))
    {
        // Base is a register, offset is immediate -> use LEA [base + imm]
        const int32_t offset = static_cast<int32_t>(offsetVal.i64);
        const Operand mem = makeMemOperand(*baseReg, offset);
        builder.append(MInstr::make(MOpcode::LEA, std::vector<Operand>{dest, mem}));
    }
    else if (baseReg)
    {
        // Both base and offset are registers -> use LEA [base + index*1]
        const Operand offsetOp = builder.makeOperandForValue(offsetVal, RegClass::GPR);
        const auto *offsetReg = std::get_if<OpReg>(&offsetOp);
        if (offsetReg)
        {
            const Operand mem = makeMemOperand(*baseReg, *offsetReg, 1, 0);
            builder.append(MInstr::make(MOpcode::LEA, std::vector<Operand>{dest, mem}));
        }
        else
        {
            // Fallback: copy base to dest, then add offset
            builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{dest, baseOp}));
            builder.append(MInstr::make(MOpcode::ADDrr, std::vector<Operand>{dest, offsetOp}));
        }
    }
    else
    {
        // Base is not a register - materialize it first
        const VReg tmpReg = builder.makeTempVReg(RegClass::GPR);
        const Operand tmp = makeVRegOperand(tmpReg.cls, tmpReg.id);
        builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{tmp, baseOp}));

        if (builder.isImmediate(offsetVal))
        {
            const int32_t offset = static_cast<int32_t>(offsetVal.i64);
            const auto tmpBaseReg = std::get<OpReg>(tmp);
            const Operand mem = makeMemOperand(tmpBaseReg, offset);
            builder.append(MInstr::make(MOpcode::LEA, std::vector<Operand>{dest, mem}));
        }
        else
        {
            const Operand offsetOp = builder.makeOperandForValue(offsetVal, RegClass::GPR);
            builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{dest, tmp}));
            builder.append(MInstr::make(MOpcode::ADDrr, std::vector<Operand>{dest, offsetOp}));
        }
    }
}

} // namespace viper::codegen::x64::lowering
